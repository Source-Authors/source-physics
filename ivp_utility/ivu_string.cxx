// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>

#include <climits> // INT_MAX / INT_MIN
#include <cstdarg>
#include <cstdio>  // vsnprintf, fputs, FILE
#include <cstdlib>
#include <cstring>
#include <iterator>  // std::size

#ifdef _WIN32
// dimhotepus: Moved up from the middle of the file.
#include <ctime>

extern "C"
__declspec( dllimport ) void __stdcall
OutputDebugStringA( const char * );
#endif

namespace
{
// dimhotepus: Locale-independent ASCII upper-casing.
[[nodiscard]] inline char ascii_upper( char c )
{
	return ( c >= 'a' && c <= 'z' ) ? static_cast< char >( c - 'a' + 'A' ) : c;
}

// dimhotepus: Single place for "OutputDebugString on Windows, stderr elsewhere".
void emit_message( const char *text )
{
#ifdef _WIN32
	OutputDebugStringA( text );
#else
	fputs( text, stderr );
#endif
}

// dimhotepus: Format into an exactly sized heap string. Replaces the 10000 byte
// stack buffer + p_strdup copy (two copies, silent truncation) used by
// p_make_string, p_make_string_fast and p_export_error. va_copy is needed
// because the first vsnprintf consumes args.
char *make_string_v( const char *templat, va_list args )
{
	va_list measure;
	va_copy( measure, args );
	const int len = vsnprintf( nullptr, 0, templat, measure );
	va_end( measure );

	if ( len < 0 ) // encoding error in the format
		return nullptr;

	const size_t size = (size_t)len + 1;
	char        *out = (char *)p_malloc( size );
	vsnprintf( out, size, templat, args );
	return out;
}

// dimhotepus: Error message was one process-wide global. Two threads calling
// p_export_error() raced on free + assign (double free / use after free).
// Now each thread owns its message. The destructor frees it at thread exit,
// a plain thread_local char * would leak one buffer per thread.
struct Error_Buffer
{
	char *text{ nullptr };

	Error_Buffer() = default;
	Error_Buffer( const Error_Buffer & ) = delete;
	Error_Buffer &operator=( const Error_Buffer & ) = delete;

	~Error_Buffer() { P_FREE( text ); }
};

thread_local Error_Buffer error_buffer;

} // namespace

void P_String::uppercase( char *str )
{
	// dimhotepus: Null guard, was a crash.
	if ( !str )
		return;

	for ( ; *str; str++ )
	{
		*str = ascii_upper( *str );
	}
}

const char *P_String::find_string( const char *str, const char *key,
                                   int upper_case )
{
	/* checks for a substring in another string
     upper_case = 0	->exact match
                = 1	-> a==A
                = 2	-> a==A && a==?
  */
	// dimhotepus: key == nullptr crashed, treat like str == nullptr.
	if ( str == nullptr || key == nullptr )
		return nullptr;

	// dimhotepus: The four copy-pasted loops differed only in the compare.
	// Same mapping as before: 0 exact, 1 ignore case, 2 '?' wildcard in key,
	// anything else ignore case + wildcard (old "default" branch).
	const bool ignore_case = upper_case != 0 && upper_case != 2;
	const bool wildcard = upper_case != 0 && upper_case != 1;

	// dimhotepus: Exact match is exactly strstr (empty key returns str too),
	// and libc implementations are far faster than a naive loop.
	if ( !ignore_case && !wildcard )
		return strstr( str, key );

	for ( ;; ++str )
	{
		const char *p1 = str;
		const char *p2 = key;

		for ( ; *p2; ++p1, ++p2 )
		{
			const char a = *p1;
			const char b = *p2;

			// Text exhausted before key: no later start can match either.
			if ( !a )
				return nullptr;

			const bool equal = ignore_case ? ascii_upper( a ) == ascii_upper( b ) : a == b;
			if ( !equal && !( wildcard && b == '?' ) )
				break;
		}

		if ( !*p2 ) // whole key matched (also covers an empty key)
			return str;
	}
}

int P_String::string_cmp( const char *str, const char *search,
                          IVP_BOOL upper_case )
/*	*	Wildcard in search string */
/*	?	any	Charakter	*/
/*	if uppercase	change all letters to uppercase */
/*	returns 0 if strings are equal -1 or +1 if left strings is
        less/greater than right string */
{
	// dimhotepus: search == nullptr crashed.
	if ( !str || !search )
		return -1;

	const char *p1 = str;
	const char *p2 = search;
	char        fsbuf[256];

	for ( ;; )
	{
		const char a = *p1;
		char       b = *p2;

		if ( b == '*' )
		{
			if ( !p2[1] )
				return 0; /* last character is wildcard */

			// dimhotepus: size_t instead of IVP_INT32, it is compared with and
			// added to size_t / pointers below.
			size_t i = 0;
			char  *d = fsbuf;
			for ( p2++; ( b = *p2 ) && ( b != '*' ); )
			{
				*( d++ ) = b;
				p2++;
				i++;
				if ( i > 250 )
					break;
			}

			if ( *p2 != '*' )
			{
				// dimhotepus: Old code did p1 += strlen( p1 ) - i and tested
				// p1 < str afterwards. Forming a pointer before the array is UB,
				// and the test used the string start instead of the current
				// position, so the segment could overlap text already matched
				// ("abc" matched "ab*bc"). Compare lengths first.
				const size_t rest = strlen( p1 );
				if ( rest < i )
					return -1;  /* neither less or greater */
				p1 += rest - i; /* check the end of the string */
				p2 -= i;
			}
			else
			{
				*d = 0;
				p1 = find_string( p1, fsbuf, (int)upper_case + 2 );
				if ( !p1 )
					return -1;
				p1 += i;
			}
			continue;
		}

		if ( !a )
			return b;
		if ( !b )
			return a;
		if ( b != '?' )
		{
			if ( upper_case ? ascii_upper( a ) != ascii_upper( b ) : a != b )
				return 1;
		}

		p1++;
		p2++;
	}
	// dimhotepus: Removed unreachable "return 0"
}

/********************************************************************************************
                                        String Replace
********************************************************************************************/

char *GBS_remove_escape( char *com ) /* \ is the escape charakter */
{
	char *result = p_strdup( com );
	// dimhotepus: Handle p_strdup failure.
	if ( !result )
		return nullptr;

	// dimhotepus: Rewritten as one flat loop (was nested switches with a
	// stray ';' and an s-- / break trick). Works in place on the copy, output is
	// never longer than input so d never passes s.
	const char *s = result;
	char       *d = result;

	while ( char ch = *s++ )
	{
		if ( ch == '\\' )
		{
			ch = *s++;
			if ( !ch ) // trailing backslash is dropped, same as before
				break;

			switch ( ch )
			{
			case 'n':
				ch = '\n';
				break;
			case 't':
				ch = '\t';
				break;
			case '0':
				ch = '\0';
				break;
			default:
				break;
			}
		}
		*d++ = ch;
	}
	*d = '\0';
	return result;
}

char *p_strdup( const char *s )
{
	// kann auch NULLen
	if ( s )
	{
		const size_t len = strlen( s ) + 1;
		char        *s2 = (char *)p_malloc( len );
		memcpy( s2, s, len );
		return s2;
	}

	return nullptr;
}

char *p_make_string_fast( const char *templat, ... )
{
	// returns an allocated string with format like ivp_message
	// NULL-Strings and empty strings allowed
	if ( !templat )
		return nullptr;

	// dimhotepus: Was a byte-for-byte copy of p_make_string with the same
	// 10000 byte truncating buffer, although the header claimed "doesn't check
	// overflow". Both now share make_string_v, no truncation and no copy.
	va_list parg;
	va_start( parg, templat ); //-V2019 //-V2018
	char *result = make_string_v( templat, parg );
	va_end( parg );
	return result;
}

char *p_make_string( const char *templat, ... )
{
	//-V524
	// returns an allocated string with format like ivp_message
	// NULL-Strings and empty strings allowed
	if ( !templat )
		return nullptr;

	va_list parg;
	va_start( parg, templat ); //-V2019 //-V2018
	char *result = make_string_v( templat, parg );
	va_end( parg );
	return result;
}

// dimhotepus: Reduce error buffer size 10000 -> 2048 as it is enough.
#define MAX_ERROR_BUFFER_LEN 2048

IVP_ERROR_STRING p_export_error( const char *templat, ... )
{
	if ( !templat )
		return nullptr;

	// for general error management... z.B. p_error_message()
	va_list parg;

	// dimhotepus: Format BEFORE freeing the old message. If a caller passes
	// p_get_error() (or a pointer into it) as an argument, the old order read
	// freed memory. Also drops the fixed 2048 byte truncating buffer.
	va_start( parg, templat ); //-V2019 //-V2018
	char *message = make_string_v( templat, parg );
	va_end( parg );

	// dimhotepus: Per-thread buffer, no lock needed.
	P_FREE( error_buffer.text );
	error_buffer.text = message;
	return error_buffer.text;
}

IVP_ERROR_STRING p_get_error()
{
	// dimhotepus: Returns the calling thread's last error. Valid until the next
	// p_export_error() on the same thread.
	return error_buffer.text;
}

void ivp_message( const char *fmt, ... )
{
	// dimhotepus: Null format is UB in vsnprintf.
	if ( !fmt )
		return;

	// for general error management...
	// dimhotepus: Format once, directly behind the prefix. Was two 2 KB buffers,
	// vsnprintf into the first, snprintf( "[havok] %s" ) into the second.
	static constexpr char   prefix[] = "[havok] ";
	static constexpr size_t prefix_len = sizeof( prefix ) - 1;

	char buffer[MAX_ERROR_BUFFER_LEN];
	memcpy( buffer, prefix, prefix_len );

	va_list args;
	va_start( args, fmt ); //-V2019 //-V2018
	vsnprintf( buffer + prefix_len, std::size( buffer ) - prefix_len, fmt, args );
	va_end( args );

	emit_message( buffer );
}

void p_print_error()
{
	// dimhotepus: p_get_error() is nullptr until the first error, passing it to
	// "%s" is UB (crash on non-glibc libcs).
	const char *error = p_get_error();

	char buffer_out[MAX_ERROR_BUFFER_LEN];
	snprintf( buffer_out, std::size( buffer_out ), "[havok] %s", error ? error : "(null)" );

	emit_message( buffer_out );
}

char *p_read_first_token( FILE *fp )
{
	// NOTE: static buffer, result is valid until the next call. Not thread safe.
	static char buffer[1024];
	// dimhotepus: sizeof instead of magic 1000 that disagreed with the buffer size.
	while ( fgets( buffer, (int)sizeof( buffer ), fp ) )
	{
		if ( buffer[0] == '#' )
			continue; // Comment
		char *tok = p_str_tok( buffer, IVP_WHITESPACE );
		if ( !tok )
			continue;
		return tok;
	}
	return nullptr;
}

char *p_get_string()
{
	// dimhotepus: nullptr instead of 0.
	char *s = p_str_tok( nullptr, "\n" );
	return p_strdup( s );
}

char *p_get_next_token()
{
	// dimhotepus: nullptr instead of 0.
	return p_str_tok( nullptr, IVP_WHITESPACE );
}

int p_get_num()
{
	// dimhotepus: atoi( nullptr ) crashed when no token was left. p_atoi is
	// null safe and returns 0, same as p_get_float does for a missing token.
	return p_atoi( p_str_tok( nullptr, IVP_WHITESPACE ) );
}

IVP_DOUBLE p_get_float()
{
	// dimhotepus: p_atof already handles nullptr, drop the duplicated parsing.
	return p_atof( p_str_tok( nullptr, IVP_WHITESPACE ) );
}

std::ptrdiff_t p_strlen( const char *s )
{
	return s && s[0] ? (std::ptrdiff_t)strlen( s ) : 0;
}

int p_strcmp( const char *s1, const char *s2 )
{
	if ( s1 == nullptr )
	{
		if ( s2 == nullptr )
			return 0;
		return 1;
	}
	if ( s2 == nullptr )
		return -1;
	return strcmp( s1, s2 );
}

char *p_str_tok( char *a, const char *deli )
{
	char *temp = strtok( a, deli );
	// Windows will always append a '0d'; maybe correct this with deli
	if ( !temp )
		return nullptr;

	// dimhotepus: Truncate at the first CR. The old loop replaced every CR but
	// stopped at the first one it wrote (it tests temp[i] for NUL), so this is
	// identical, with strchr instead of a byte loop.
	if ( char *cr = strchr( temp, '\r' ) )
		*cr = '\0';
	return temp;
}

IVP_DOUBLE p_atof( const char *s )
{
	if ( !s )
		return 0.0;
	// dimhotepus: strtod, not strtof. IVP_DOUBLE is double (float only with
	// IVP_NO_DOUBLE), strtof rounded to float first and lost ~9 digits
	// ( "0.1" -> 0.10000000149 ). Also removed stray ';' after the body.
	return (IVP_DOUBLE)strtod( s, nullptr );
}

int p_atoi( const char *s )
{
	if ( !s )
		return 0;
	// dimhotepus: atoi is UB on overflow. strtoll + clamp is defined on all
	// targets (long is 32 bit on Windows, 64 bit on Linux / macOS).
	const long long v = strtoll( s, nullptr, 10 );
	if ( v > INT_MAX )
		return INT_MAX;
	if ( v < INT_MIN )
		return INT_MIN;
	return (int)v;
}

#ifdef _WIN32
// dimhotepus: long long instead of long for 64 bits.
long long p_get_time()
{ // returns seconds since 1970
	// dimhotepus: time( nullptr ) instead of an unused time_t local.
	return (long long)time( nullptr );
}

// there's no strcasecmp on Windows !?!

// not sure, whether this implementation is entirely correct
int strcasecmp( const char *a, const char *b )
{
	// chris, Sept 2000
	// dimhotepus: _stricmp, the unprefixed stricmp is a deprecated POSIX name.
	return _stricmp( a, b );
}
#endif
