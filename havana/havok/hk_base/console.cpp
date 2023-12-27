
#include <hk_base/base.h>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>

#ifdef WIN32
#	ifndef WIN32_LEAN_AND_MEAN
#		define	WIN32_LEAN_AND_MEAN
#	endif
#	ifdef _XBOX
#		include <xtl.h>
#	else
#		include <windows.h>
#	endif
#endif

hk_Console *hk_Console::m_console = HK_NULL;
hk_Console hk_Console::m_default_console_buffer;

hk_Console *hk_Console::get_instance()
{
	if ( !m_console )
	{
		m_console = &m_default_console_buffer;
	}
	return m_console;
}
#define MAX_ERROR_BUFFER_LEN 2048

void hk_Console::printf( const char *fmt, ...)
{
    va_list args;
    char buffer[MAX_ERROR_BUFFER_LEN];
    va_start(args, fmt);
    vsprintf(buffer, fmt, args);
    va_end(args);

#ifdef WIN32
    OutputDebugString(buffer);
#else
    fprintf(stderr, "%s", buffer);
#endif
}

void hk_Console::flush()
{
#ifndef WIN32
#ifndef HK_PS2
	fflush(stdout);
#endif
#endif
}


void hk_Console::exit( int code )
{
	::exit(code);
}
