// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PUBLIC

#ifndef _IVP_U_TYPES_INCLUDED
#define _IVP_U_TYPES_INCLUDED

/********************************************************************************
 *                    Standard headers (portable across all targets)
 ********************************************************************************/
#include <algorithm> // std::min
#include <csignal>   // raise, SIGINT (POSIX and MSVC)
#include <cstddef>   // std::size_t, std::ptrdiff_t, std::nullptr_t
#include <cstdint>   // fixed-width integer types
#include <cstdio>    // std::fprintf (assertion output)
#include <cstdlib>
#include <cstring> // std::memset

/********************************************************************************
 *                    Operating system detection
 ********************************************************************************/
#if defined( _WIN32 )
#define IVP_OS_WINDOWS 1
#elif defined( __APPLE__ ) && defined( __MACH__ )
#define IVP_OS_MACOS 1
#elif defined( __linux__ ) || defined( _LINUX )
#define IVP_OS_LINUX 1
#else
#error "Unsupported operating system. Supported: Windows, Linux, macOS."
#endif

#if defined( IVP_OS_LINUX ) || defined( IVP_OS_MACOS )
#define IVP_OS_POSIX 1
#endif

/********************************************************************************
 *                    Architecture detection
 ********************************************************************************/
#if defined( _M_X64 ) || defined( __x86_64__ ) || defined( __amd64__ )
#define IVP_ARCH_X86_64 1
#elif defined( _M_IX86 ) || defined( __i386__ )
#define IVP_ARCH_X86 1
#elif defined( _M_ARM64 ) || defined( __aarch64__ ) || defined( __arm64__ )
#define IVP_ARCH_ARM64 1
#else
#error "Unsupported architecture. Supported: x86, x86-64, arm64."
#endif

#if defined( IVP_ARCH_X86 ) || defined( IVP_ARCH_X86_64 )
#define IVP_ARCH_X86_ANY 1
#endif

// All supported targets are little-endian.
#if defined( __BYTE_ORDER__ ) && defined( __ORDER_BIG_ENDIAN__ ) && \
( __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__ )
#error "Big-endian targets are not supported."
#endif

/********************************************************************************
 *                    Configuration flags
 ********************************************************************************/
// P3-specific code paths: only meaningful for 32-bit x86 on Windows.
#if defined( IVP_OS_WINDOWS ) && defined( IVP_ARCH_X86 )
#define IVP_PIII /* set for P3 specific code */
#endif
// #define IVP_WILLAMETTE /* set for Willamette specific code */
// #define IVP_WMT_ALIGN  /* set to compile with MS but Willamette compatible */
// #define IVP_NO_DOUBLE  /* set if processor has no double floating point unit,
//                           or lib should use float only */

#define IVP_VECTOR_UNIT_FLOAT  /* set if extra data is inserted to support vector units */
#define IVP_VECTOR_UNIT_DOUBLE /* set if extra data should be inserted to utilize double vector units */

#ifdef IVP_WILLAMETTE
#define IVP_IF_WILLAMETTE_OPT( cond ) if ( cond )
#else
#define IVP_IF_WILLAMETTE_OPT( cond ) if ( 0 )
#endif

#if defined( IVP_WILLAMETTE ) || defined( IVP_PIII ) || defined( IVP_WMT_ALIGN )
#define IVP_WINDOWS_ALIGN16
#endif

/********************************************************************************
 *                    Debug / release configuration
 ********************************************************************************/
#if defined( NDEBUG ) && defined( DEBUG )
#error "Both DEBUG and NDEBUG are defined. Define only one of them (check your build system)."
#endif

/********************************************************************************
 *                    Breakpoint / fatal signal
 ********************************************************************************/
#if defined( IVP_OS_WINDOWS )
#include <intrin.h>
#define IVP_BREAK() __debugbreak()
#else
#define IVP_BREAK() raise( SIGINT ) /* send fatal signal */
#endif

// Kept for source compatibility with existing call sites.
#define CORE IVP_BREAK()

/********************************************************************************
 *                    Assertions
 ********************************************************************************/
#ifdef NDEBUG
#define IVP_ASSERT( cond ) ( (void)0 )
#define IVP_USE( a )       ( (void)( a ) )
#define IVP_IF( flag )     if ( 0 == 1 )
#else
#define IVP_ASSERT( cond )                                                           \
	do                                                                               \
	{                                                                                \
		if ( !( cond ) )                                                             \
		{                                                                            \
			std::fprintf( stderr, "\nASSERTION FAILURE: %s\nFILE: %s\nLINE: %d\n\n", \
			              #cond, __FILE__, __LINE__ );                               \
			IVP_BREAK();                                                             \
		}                                                                            \
	} while ( false )
#define IVP_USE( a )   ( (void)( a ) )
#define IVP_IF( flag ) if ( flag )
#endif

/********************************************************************************
 *                    Base scalar types
 ********************************************************************************/
using IVP_FLOAT = float;
using IVP_INT32 = std::int32_t;
using IVP_UINT32 = std::uint32_t;

// Note: these three typedefs match the platform typedefs of the same names
// (for example in <sys/types.h>). Redeclaring a typedef to the same type is
// legal C++, so this is safe.
using uchar = unsigned char;
using ushort = unsigned short;
using uint = unsigned int;

using hk_intp = std::ptrdiff_t;
using hk_uintp = std::size_t;

/********************************************************************************
 *                    Time
 ********************************************************************************/
#ifndef IVP_NO_DOUBLE
using IVP_DOUBLE = double;
class IVP_Time
{
	double seconds;

public:
	void   operator+=( double val ) { seconds += val; }
	double get_seconds() const { return seconds; }
	double get_time() const { return seconds; } // for debugging
	double operator-( const IVP_Time &b ) const
	{
		return this->seconds - b.seconds;
	}
	void     operator-=( const IVP_Time &b ) { this->seconds -= b.seconds; }
	IVP_Time operator+( double val ) const
	{
		IVP_Time result;
		result.seconds = this->seconds + val;
		return result;
	}

	IVP_Time() :
		IVP_Time{ 0 }
	{
	}
	IVP_Time( double time ) { seconds = time; }
};

#else
using IVP_DOUBLE = float;
class IVP_Time
{
	float seconds;
	float sub_seconds;

public:
	void operator+=( float val )
	{
		sub_seconds += val;
		while ( sub_seconds > 1.0f )
		{
			seconds++;
			sub_seconds -= 1.0f;
		}
	}
	float get_seconds() const { return seconds; }
	float get_time() const { return seconds + sub_seconds; } // for debugging
	float operator-( const IVP_Time &b ) const
	{
		return ( this->seconds - b.seconds ) + this->sub_seconds - b.sub_seconds;
	}
	void operator-=( const IVP_Time &b )
	{
		this->seconds -= b.seconds;
		this->sub_seconds -= b.sub_seconds;
		while ( sub_seconds > 1.0f )
		{
			seconds++;
			sub_seconds -= 1.0f;
		}
		while ( sub_seconds < 0.0f )
		{
			seconds--;
			sub_seconds += 1.0f;
		}
	}
	IVP_Time operator+( float val ) const
	{
		IVP_Time result;
		result.seconds = this->seconds;
		result.sub_seconds = this->sub_seconds + val;
		while ( result.sub_seconds > 1.0f )
		{
			result.seconds++;
			result.sub_seconds -= 1.0f;
		}
		return result;
	}
	IVP_Time() :
		IVP_Time{ 0 }
	{
	}
	IVP_Time( float time )
	{
		seconds = float( static_cast< int >( time ) );
		sub_seconds = time - static_cast< int >( time );
	}
};
#endif

using IVP_HTIME = IVP_FLOAT;

/********************************************************************************
 *                    Error handling
 ********************************************************************************/
using IVP_ERROR_STRING = const char *;
constexpr inline std::nullptr_t IVP_NO_ERROR{ nullptr };

/********************************************************************************
 *                    Simple base types
 ********************************************************************************/
using IVP_Time_CODE = int; // Internal enumeration for time handles.
using ivp_u_bool = int;    // must be int!!

// boolean enumeration, compatible to C++ bool, ivp_u_bool
enum IVP_BOOL
{
	IVP_FALSE = 0,
	IVP_TRUE = 1
};

enum IVP_RETURN_TYPE
{
	IVP_FAULT = 0,
	IVP_OK = 1
};

// Set this to whatever your toolchain needs (for example __cdecl on 32-bit MSVC).
#define IVP_CDECL

/********************************************************************************
 *                    Alignment
 ********************************************************************************/
// Default alignment macro. Empty by default, as in the original header.
// If you need 16-byte alignment on a type, use alignas(16) on it directly.
#if !defined( IVP_ALIGN_16 )
#define IVP_ALIGN_16
#endif

/********************************************************************************
 *                    Memory management
 ********************************************************************************/
#if defined( IVP_OS_MACOS )
#include <malloc/malloc.h>
#elif defined( IVP_OS_LINUX ) || defined( IVP_OS_WINDOWS )
#include <malloc.h>
#endif

char *IVP_CDECL p_calloc( std::size_t nelem, std::size_t size );
void *IVP_CDECL p_realloc( void *memblock, std::size_t size );
void *IVP_CDECL p_malloc( std::size_t size );
void IVP_CDECL  p_free( void *data );

// Aligned allocation. Implementations must use _aligned_malloc/_aligned_free on
// Windows and posix_memalign/free on Linux and macOS. Do not mix allocators.
void *IVP_CDECL ivp_malloc_aligned( std::size_t size, unsigned short alignment );
void *IVP_CDECL ivp_calloc_aligned( std::size_t size, unsigned short alignment );
void IVP_CDECL  ivp_free_aligned( void *data );

// Byte swapping (portable, no architecture-specific intrinsics required).
void IVP_CDECL ivp_byte_swap4( uint &fourbytes );
void IVP_CDECL ivp_byte_swap2( ushort &twobytes );

char *p_strdup( const char *s );

extern void ivp_memory_check( void *a );

#define P_DELETE( a )                 \
	do                                \
	{ /*ivp_memory_check((void*)a);*/ \
		delete ( a );                 \
		( a ) = nullptr;              \
	} while ( false )
#define P_DELETE_THIS( a )            \
	do                                \
	{ /*ivp_memory_check((void*)a);*/ \
		delete ( a );                 \
	} while ( false )
#define P_DELETE_ARRAY( a )           \
	do                                \
	{ /*ivp_memory_check((void*)a);*/ \
		delete[] ( a );               \
		( a ) = 0;                    \
	} while ( false )

#define P_FREE_ALIGNED( a )                \
	do                                     \
	{                                      \
		if ( a )                           \
		{ /*ivp_memory_check((void*)a);*/  \
			ivp_free_aligned( (void *)( a ) ); \
			( a ) = nullptr;                   \
		}                                  \
	} while ( false )
#define P_FREE( a )                       \
	do                                    \
	{                                     \
		if ( a )                          \
		{ /*ivp_memory_check((void*)a);*/ \
			p_free( (char *)( a ) );          \
			( a ) = nullptr;                  \
		}                                 \
	} while ( false )

// dimhotepus: Macro -> template function.
template< typename T >
inline void P_MEM_CLEAR( T *a )
{
	memset( a, 0, sizeof( T ) );
}

// dimhotepus: Macro -> template function.
// dimhotepus: Deprecate as hacky.
template< typename T >
[[deprecated( "Horrible hack, do not use" )]] inline void P_MEM_CLEAR_M4( T *a )
{
	memset( (unsigned char *)a + sizeof( void * ), 0, sizeof( T ) - sizeof( void * ) );
}

// dimhotepus: Macro -> template function.
template< typename T, size_t size >
inline void P_MEM_CLEAR_ARRAY( T ( &clss )[size], size_t elems )
{
	HK_ASSERT( size >= elems );
	memset( clss, 0, sizeof( T ) * min( size, elems ) );
}

// used for division checking
constexpr inline float P_FLOAT_EPS{ 1e-10f };
// float resolution for numbers < 1.0
constexpr inline float P_FLOAT_RES{ 1e-6f };
constexpr inline float P_FLOAT_MAX{ 1e16f };

#ifdef IVP_NO_DOUBLE
#define IVP_PI                       3.14159265358979323846f /* pi */
#define IVP_PI_2                     1.57079632679489661923f /* pi/2 */
#define P_DOUBLE_MAX                 P_FLOAT_MAX
#define P_DOUBLE_RES                 P_FLOAT_RES // double resolution for numbers < 1.0
#define IVP_3D_SOLVER_NULLSTELLE_EPS 3e-3f
#define P_DOUBLE_EPS                 P_FLOAT_EPS // used for division checking
#define P_MAX_WORLD_DOUBLE           3000.0f     // max world koords
#else
constexpr inline double IVP_PI{ 3.14159265358979323846 };   /* pi */
constexpr inline double IVP_PI_2{ 1.57079632679489661923 }; /* pi/2 */
constexpr inline double P_DOUBLE_MAX{ 1e20 };
// double resolution for numbers < 1.0
constexpr inline double P_DOUBLE_RES{ 1E-12 };
constexpr inline double IVP_3D_SOLVER_NULLSTELLE_EPS{ 1e-8 };
constexpr inline double P_DOUBLE_EPS{ 1e-10 };       // used for division checking
constexpr inline double P_MAX_WORLD_DOUBLE{ 10000 }; // max world koords
#endif

constexpr inline float P_MAX_OBJECT_SIZE{ 1000.0f };
// 10 mm min edge len of polygon objects
constexpr inline float P_MIN_EDGE_LEN{ 0.01f };
// effective IVP_DOUBLE resolution for world coords
constexpr inline float P_RES_EPS{ /*P_MAX_WORLD_DOUBLE*/ 10000.0f * P_FLOAT_RES };

void      ivp_srand( int seed );
IVP_FLOAT ivp_rand(); // returns [0 .. 1]

#if defined( PSXII )
#define IVP_NO_ALLOCA
#endif

#if defined( IVP_OS_WINDOWS )
#if defined( IVP_PIII ) || defined( IVP_WILLAMETTE )
#if defined( IVP_PIII )
#define IVP_PREFETCH_CLINE_SIZE 0x20
#else
#define IVP_PREFETCH_CLINE_SIZE 0x40
#endif
#define IVP_IF_PREFETCH_ENABLED( x ) if ( x )
#include <xmmintrin.h>
#define IVP_PREFETCH( pntr, offset ) \
	_mm_prefetch( hk_intp( offset ) + (char *)pntr, _MM_HINT_T1 )
#endif

#elif defined( PSXII ) && 0
#define IVP_PREFETCH_CLINE_SIZE      0x40
#define IVP_IF_PREFETCH_ENABLED( x ) if ( x )
#define IVP_PREFETCH( __addr, __offs ) \
	( { asm volatile( "pref 0,%1(%0)" : : "r"( __addr ), "i"( __offs ) ); } )
#endif

#if !defined( IVP_PREFETCH )
#define IVP_PREFETCH_CLINE_SIZE 0x100
#define IVP_PREFETCH( pntr, offset )
#ifdef DEBUG
#define IVP_IF_PREFETCH_ENABLED( x ) if ( 1 )
#else
#define IVP_IF_PREFETCH_ENABLED( x ) if ( 0 )
#endif
#define IVP_PREFETCH_BLOCK( a, b ) /* IVP_ASSERT(b < 128);*/
#else

#define IVP_PREFETCH_BLOCK( pntr, size )                                \
	do                                                                  \
	{                                                                   \
		IVP_PREFETCH( pntr, 0 );                                        \
		if ( size > IVP_PREFETCH_CLINE_SIZE )                           \
			IVP_PREFETCH( pntr, IVP_PREFETCH_CLINE_SIZE );              \
		if ( size > 2 * IVP_PREFETCH_CLINE_SIZE )                       \
			IVP_PREFETCH( pntr, 2 * IVP_PREFETCH_CLINE_SIZE );          \
		if ( size > 3 * IVP_PREFETCH_CLINE_SIZE )                       \
			IVP_PREFETCH( pntr, 3 * IVP_PREFETCH_CLINE_SIZE );          \
		/*if ( size > 4*IVP_PREFETCH_CLINE_SIZE) IVP_PREFETCH( pntr, \
     * 4*IVP_PREFETCH_CLINE_SIZE);*/ \
		/*IVP_PREFETCH( pntr, (size - sizeof(void *))) */               \
	} while ( false )

#endif

#ifndef IVP_FAST_WHEELS_ENABLED
#define IVP_FAST_WHEELS_ENABLED
#endif // IVP_FAST_WHEELS_ENABLED

// define this if you have the havana constraints module
#ifndef HAVANA_CONSTRAINTS
// #define HAVANA_CONSTRAINTS
#endif // HAVANA_CONSTRAINTS

// define this if you have the havok MOPP module
#ifndef HAVOK_MOPP
// #define HAVOK_MOPP
#endif // HAVOK_MOPP

#endif
