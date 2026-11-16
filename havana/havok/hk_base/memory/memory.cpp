#include "hk_base/base.h"

#include <cstdint>
#include <cstdlib>
#include <mutex>

#include <cstdlib> // posix_memalign

#if defined( _WIN32 )
// dimhotepus: _aligned_malloc / _aligned_free live here.
#include <malloc.h>
#endif

namespace
{

// dimhotepus: Make header exactly 16 bytes on x86, x86-64 and arm64 (was 24 on
// 64-bit: size_t + enum + int32[2] + padding), so user data stays 16-byte
// aligned (SSE / NEON) and the "16 bytes wasted" comment is true.
// Unnamed enum had implementation defined underlying type, use fixed uint32.
struct hk_Memory_With_Size
{
	hk_size_t m_size;

	static constexpr hk_uint32 MAGIC_MEMORY_WITH_SIZE = 0x2345656U;

	hk_uint32 m_magic;
	hk_uint32 m_dummy[( 16 - sizeof( hk_size_t ) - sizeof( hk_uint32 ) ) /
	                  sizeof( hk_uint32 )];
};

static_assert( sizeof( hk_Memory_With_Size ) == 16 );

// dimhotepus: Every row size is multiple of 16 now. Old row 1 was 8 bytes, but
// hk_Memory_Elem is 16 bytes on 64-bit => writing m_magic overflowed the
// block. Also guarantees 16-byte alignment of all pooled blocks.
// Not constexpr: HK_BREAK is not a constant expression.
inline hk_uchar size_to_row( hk_uint16 size )
{
	static_assert( HK_MEMORY_MAX_ROW == 12 );

	if ( size <= 16 )
		return 1;
	else if ( size <= 32 )
		return 2;
	else if ( size <= 48 )
		return 3; //-V112
	else if ( size <= 64 )
		return 4; //-V112
	else if ( size <= 96 )
		return 5;
	else if ( size <= 128 )
		return 6;
	else if ( size <= 160 )
		return 7;
	else if ( size <= 192 )
		return 8;
	else if ( size <= 256 )
		return 9;
	else if ( size <= 384 )
		return 10;
	else if ( size <= 512 )
		return 11;
	else
	{
		HK_BREAK;
		return 0;
	}
}

} // namespace

void hk_Memory::init_memory( char *buffer, hk_size_t buffer_size )
{
	static_assert( sizeof( hk_Memory_Elem ) <= 16 );
	static_assert( sizeof( hk_Memory_Block ) == 64 );

	// dimhotepus: Thread safety.
	std::lock_guard< std::mutex > lock( m_mutex );

	// dimhotepus: User buffer may be unaligned, align to 16 bytes.
	if ( buffer )
	{
		const auto addr = reinterpret_cast< std::uintptr_t >( buffer );
		const auto aligned = ( addr + 15U ) & ~static_cast< std::uintptr_t >( 15U );

		hk_size_t adjust = static_cast< hk_size_t >( aligned - addr );
		if ( adjust > buffer_size )
			adjust = buffer_size;

		buffer += adjust;
		buffer_size -= adjust;
	}

	m_memory_start = buffer;
	m_used_end = buffer;
	m_memory_end = m_used_end + buffer_size;
	// dimhotepus: Do not reset m_allocated_memory_blocks here (leaked the old
	// chain, now destructor frees it). Initialized to nullptr in class.

	for ( int i = HK_MEMORY_MAX_ROW - 1; i >= 0; i-- )
	{
		m_free_list[i] = nullptr;
		m_blocks_in_use[i] = 0;
		// dimhotepus: Row 0 is never used, was left uninitialized.
		m_row_to_size[i] = 0;
	}

	for ( hk_uint16 j = 0; j <= HK_MEMORY_MAX_SIZE_SMALL_BLOCK; j++ )
	{
		hk_uchar row = size_to_row( j );
		m_size_to_row[j] = row;
		m_row_to_size[row] = j;
	}
	{ // statistics
		for ( auto &&s : m_statistics )
		{
			s.m_max_size_in_use = 0U;
			s.m_size_in_use = 0U;
			s.m_n_allocates = 0U;
			s.m_blocks_in_use = 0U;
		}
	}
}

hk_Memory::hk_Memory()
{
	// dimhotepus: Removed memset of statistics, init_memory zeroes them.
#ifdef HK_MEMORY_POOL_INITIAL_SIZE
	// dimhotepus: Was init_memory( new char[N], 0 ): pool size was 0 (pool never
	// used) and buffer leaked. Use real size, own the buffer, free in dtor.
	char *buffer = static_cast< char * >(
	hk_Memory::aligned_malloc( HK_MEMORY_POOL_INITIAL_SIZE, HK_MEMORY_CACHE_ALIGNMENT ) );
	init_memory( buffer, buffer ? HK_MEMORY_POOL_INITIAL_SIZE : 0 );
	m_owned_buffer = buffer;
#else
	init_memory( nullptr, 0 );
#endif
}

hk_Memory::hk_Memory( char *buffer, hk_size_t buffer_size )
{
	init_memory( buffer, buffer_size );
}

// dimhotepus: Destructor was defaulted and leaked all extra blocks.
hk_Memory::~hk_Memory()
{
	while ( m_allocated_memory_blocks )
	{
		hk_Memory_Block *next = m_allocated_memory_blocks->m_next;
		hk_Memory::aligned_free( m_allocated_memory_blocks );
		m_allocated_memory_blocks = next;
	}

	hk_Memory::aligned_free( m_owned_buffer );
	m_owned_buffer = nullptr;
}

void hk_Memory::record_allocate( hk_MEMORY_CLASS cl, hk_size_t size )
{
#ifdef HK_MEMORY_ENABLE_STATISTICS
	hk_Memory_Statistics &s = m_statistics[static_cast< unsigned >( cl )];
	s.m_size_in_use += size;
	s.m_blocks_in_use += 1U;
	s.m_n_allocates += 1U;
	if ( s.m_size_in_use > s.m_max_size_in_use )
	{
		s.m_max_size_in_use = s.m_size_in_use;
	}
#else
	(void)cl;
	(void)size;
#endif
}

void hk_Memory::record_deallocate( hk_MEMORY_CLASS cl, hk_size_t size )
{
#ifdef HK_MEMORY_ENABLE_STATISTICS
	hk_Memory_Statistics &s = m_statistics[static_cast< unsigned >( cl )];
	s.m_size_in_use -= size;
	s.m_blocks_in_use -= 1U;
#else
	(void)cl;
	(void)size;
#endif
}

// dimhotepus: Caller holds m_mutex, size <= HK_MEMORY_MAX_SIZE_SMALL_BLOCK.
// Big blocks are handled in allocate() outside of the lock.
void *hk_Memory::allocate_real( hk_size_t size )
{
	HK_ASSERT( size <= HK_MEMORY_MAX_SIZE_SMALL_BLOCK );

	const hk_uchar row = m_size_to_row[size];
	size = m_row_to_size[row];

	// dimhotepus: Compare remaining bytes instead of forming pointer past the
	// end of the buffer (UB). Also handles nullptr pool.
	if ( static_cast< hk_size_t >( m_memory_end - m_used_end ) < size )
	{
		// dimhotepus: Removed hkprintf here, it may allocate => deadlock.
		auto *b = static_cast< hk_Memory_Block * >( hk_Memory::aligned_malloc(
		sizeof( hk_Memory_Block ) + HK_MEMORY_EXTRA_BLOCK_SIZE,
		HK_MEMORY_CACHE_ALIGNMENT ) );
		// dimhotepus: Was dereferenced without null check.
		if ( !b )
			return nullptr;

		b->m_next = m_allocated_memory_blocks;
		m_allocated_memory_blocks = b;
		m_memory_start = reinterpret_cast< char * >( b + 1 );
		m_used_end = m_memory_start;
		m_memory_end = m_used_end + HK_MEMORY_EXTRA_BLOCK_SIZE;
	}

	void *result = m_used_end;
	m_used_end += size;

	// allocate rest to get make sure the alignment is ok
	// dimhotepus: Was calling deallocate() with DUMMY class: it underflowed
	// DUMMY statistics and needed a blocks_in_use save / restore hack. Push to
	// the free list directly. Also used strict '<' and lost the last block.
	hk_size_t allocated_size = size;
	while ( allocated_size < 256U &&
	        static_cast< hk_size_t >( m_memory_end - m_used_end ) >= size )
	{
		auto *e = reinterpret_cast< hk_Memory_Elem * >( m_used_end );
		e->m_magic = HK_MEMORY_MAGIC_NUMBER;
		e->m_next = m_free_list[row];
		m_free_list[row] = e;

		m_used_end += size;
		allocated_size += size;
	}
	return result;
}

void *hk_Memory::allocate( hk_size_t size, hk_MEMORY_CLASS cl )
{
	HK_ASSERT( static_cast< unsigned >( cl ) <
	           static_cast< unsigned >( hk_MEMORY_CLASS::HK_MEMORY_CLASS_MAX ) );

#ifdef HK_MEMORY_ENABLE_DEBUG_CHECK
	if ( size > static_cast< hk_size_t >( -1 ) - sizeof( hk_Memory_With_Size ) )
		return nullptr;

	auto *x = static_cast< hk_Memory_With_Size * >( hk_Memory::aligned_malloc(
	size + sizeof( hk_Memory_With_Size ), HK_MEMORY_CACHE_ALIGNMENT ) );
	if ( !x )
		return nullptr;

	x->m_size = size;
	x->m_magic = hk_Memory_With_Size::MAGIC_MEMORY_WITH_SIZE;

	{
		std::lock_guard< std::mutex > lock( m_mutex );
		record_allocate( cl, size );
	}
	return x + 1;
#else
	if ( size <= HK_MEMORY_MAX_SIZE_SMALL_BLOCK )
	{
		const hk_uchar row = m_size_to_row[size];

		// dimhotepus: Free lists, counters and statistics are shared state.
		std::lock_guard< std::mutex > lock( m_mutex );

		void           *result;
		hk_Memory_Elem *n = m_free_list[row];
		if ( n )
		{
			m_free_list[row] = n->m_next;
			n->m_magic = 0;
			result = n;
		}
		else
		{
			result = allocate_real( size );
		}

		// dimhotepus: Count only successful allocations.
		if ( result )
		{
			m_blocks_in_use[row]++;
			record_allocate( cl, size );
		}
		return result;
	}

	// dimhotepus: Big blocks go to system allocator without holding the lock.
#ifdef HK_CHECK
	hkprintf( "big block allocated size %zu\n", static_cast< size_t >( size ) );
#endif
	void *result = hk_Memory::aligned_malloc( size, HK_MEMORY_CACHE_ALIGNMENT );
	if ( result )
	{
		std::lock_guard< std::mutex > lock( m_mutex );
		record_allocate( cl, size );
	}
	return result;
#endif
}

void hk_Memory::deallocate( void *p, hk_size_t size, hk_MEMORY_CLASS cl )
{
	// dimhotepus: Freeing nullptr must be a no-op (operator delete( nullptr )
	// used to push nullptr to the free list and crash on next allocation).
	if ( !p )
		return;

	HK_ASSERT( static_cast< unsigned >( cl ) <
	           static_cast< unsigned >( hk_MEMORY_CLASS::HK_MEMORY_CLASS_MAX ) );

#ifdef HK_MEMORY_ENABLE_DEBUG_CHECK
	{
		auto *x = static_cast< hk_Memory_With_Size * >( p );
		x--;
		HK_ASSERT( x->m_magic == hk_Memory_With_Size::MAGIC_MEMORY_WITH_SIZE );
		HK_ASSERT( x->m_size == size );
		hk_Memory::aligned_free( x );
	}

	{
		std::lock_guard< std::mutex > lock( m_mutex );
		record_deallocate( cl, size );
	}
#else
	if ( size <= HK_MEMORY_MAX_SIZE_SMALL_BLOCK )
	{
		auto *me = static_cast< hk_Memory_Elem * >( p );
		const hk_uchar row = m_size_to_row[size];

		std::lock_guard< std::mutex > lock( m_mutex );

		m_blocks_in_use[row]--;
		me->m_next = m_free_list[row];
		HK_ASSERT( me->m_magic != HK_MEMORY_MAGIC_NUMBER );
		me->m_magic = HK_MEMORY_MAGIC_NUMBER;
		m_free_list[row] = me;

		record_deallocate( cl, size );
	}
	else
	{
		hk_Memory::aligned_free( p );

		std::lock_guard< std::mutex > lock( m_mutex );
		record_deallocate( cl, size );
	}
#endif
}

void *hk_Memory::allocate_debug( hk_size_t n, const char * /*file*/,
                                 int /*line*/ )
{
	return new char[n];
}

void hk_Memory::deallocate_debug( void *p, hk_size_t /*n*/,
                                  const char * /*file*/, int /*line*/ )
{
	delete[] static_cast< char * >( p );
}

void *hk_Memory::allocate_and_store_size( hk_size_t       byte_size,
                                          hk_MEMORY_CLASS cl )
{
	// dimhotepus: Guard overflow and allocation failure (x was dereferenced
	// unconditionally).
	if ( byte_size > static_cast< hk_size_t >( -1 ) - sizeof( hk_Memory_With_Size ) )
		return nullptr;

	auto *x = static_cast< hk_Memory_With_Size * >(
	this->allocate( byte_size + sizeof( hk_Memory_With_Size ), cl ) );
	if ( !x )
		return nullptr;

	x->m_size = byte_size;
	x->m_magic = hk_Memory_With_Size::MAGIC_MEMORY_WITH_SIZE;
	return x + 1;
}

void hk_Memory::deallocate_stored_size( void *p, hk_MEMORY_CLASS cl )
{
	if ( p )
	{
		auto *x = static_cast< hk_Memory_With_Size * >( p );
		x--;

		HK_ASSERT( x->m_magic == hk_Memory_With_Size::MAGIC_MEMORY_WITH_SIZE );
		this->deallocate( x, x->m_size + sizeof( hk_Memory_With_Size ), cl );
	}
}

void *hk_Memory::aligned_malloc( hk_size_t size, hk_size_t alignment )
{
	// dimhotepus: alignment must be power of two.
	HK_ASSERT( alignment && ( alignment & ( alignment - 1 ) ) == 0 );

	// dimhotepus: std::aligned_alloc requires size to be multiple of alignment
	// (macOS returns nullptr otherwise), is missing in MSVC and macOS < 10.15.
	// Round up and use _aligned_malloc on Windows, posix_memalign elsewhere
	// (Linux, macOS; x86, x86-64, arm64). Zero size is bumped to 1 as malloc( 0 )
	// may legally return nullptr.
	if ( size == 0 )
		size = 1;

	if ( size > static_cast< hk_size_t >( -1 ) - ( alignment - 1 ) )
		return nullptr;
	size = ( size + alignment - 1 ) & ~( alignment - 1 );

#if defined( _WIN32 )
	return _aligned_malloc( size, alignment );
#else
	// posix_memalign needs alignment >= sizeof( void * ).
	if ( alignment < sizeof( void * ) )
		alignment = sizeof( void * );

	void *result = nullptr;
	return std::posix_memalign( &result, alignment, size ) == 0 ? result : nullptr;
#endif
}

void hk_Memory::aligned_free( void *data )
{
#if defined( _WIN32 )
	_aligned_free( data );
#else
	std::free( data );
#endif
}

hk_Memory *hk_Memory::get_instance()
{
	// dimhotepus: Function-local static init is thread-safe (C++11). Instance
	// is intentionally never destroyed: objects with static storage duration
	// that free memory in their destructors would otherwise use a destroyed
	// allocator (static destruction order fiasco).
	static hk_Memory *const s_memory_instance = new hk_Memory;
	return s_memory_instance;
}