#include <hk_base/base.h>
#include <hk_base/memory/memory.h>
#include <climits>
#include <cstring>

void hk_Array_Base::alloc_mem( int size, int num)
{
	HK_ASSERT( size >= 0 && num >= 0 );
	HK_ASSERT( num <= USHRT_MAX );

	m_elems = hk_allocate<char>( size * num, hk_MEMORY_CLASS::HK_MEMORY_CLASS_ARRAY );
	m_memsize = num;
}

void hk_Array_Base::grow_mem( int size )
{
	HK_ASSERT( size >= 0 );
	HK_ASSERT( ((int)m_memsize) * 2 <= USHRT_MAX );

	int new_memsize = m_memsize + m_memsize;
	if (!new_memsize) {
		new_memsize = 2;
	}

	char *new_array = hk_allocate<char>( new_memsize * size, hk_MEMORY_CLASS::HK_MEMORY_CLASS_ARRAY );

	if( m_elems )
		memcpy( new_array, m_elems, m_memsize * size );

	if ( m_elems && (m_elems != (char *)(this + 1)))
	{
		hk_deallocate( m_elems, m_memsize * size, hk_MEMORY_CLASS::HK_MEMORY_CLASS_ARRAY );
	}
	m_memsize = new_memsize;
	m_elems = new_array;
}

void hk_Array_Base::grow_mem( int size, int n_elems )
{
	HK_ASSERT( size >= 0 );
	HK_ASSERT( n_elems >= 0 );

	int new_memsize = m_memsize + n_elems;

	HK_ASSERT( new_memsize <= USHRT_MAX );

	char *new_array = hk_allocate<char>( new_memsize * size, hk_MEMORY_CLASS::HK_MEMORY_CLASS_ARRAY );
	if (!new_array) HK_BREAK;

	memcpy( new_array, m_elems, m_memsize * size );
	if ( m_elems && (m_elems != (char *)(this + 1)))
	{
		hk_deallocate( m_elems, m_memsize * size, hk_MEMORY_CLASS::HK_MEMORY_CLASS_ARRAY );
	}
	m_memsize = new_memsize;
	m_elems = new_array;
}

