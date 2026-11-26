// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>

#include "ivu_memory.hxx"

#include <cstddef>
#include <cstdint>
#include <cstdlib> // malloc / calloc / realloc / free, _byteswap_* on MSVC
#include <cstring>

void ivp_memory_check( [[maybe_unused]] void *a ) {}

void ivp_byte_swap4( uint &fourbytes )
{
#if defined( _WIN32 )
	fourbytes = _byteswap_ulong( fourbytes );
	// dimhotepus: Fast builtin byte swaps.
#elif defined( __clang__ ) || defined( __GNUC__ )
	fourbytes = __builtin_bswap32( fourbytes );
#else
	// dimhotepus: Union type punning is UB in C++; shifts are well defined and compilers
	// recognize them as bswap.
	fourbytes = ( fourbytes >> 24 ) | ( ( fourbytes >> 8 ) & 0x0000ff00U ) |
	            ( ( fourbytes << 8 ) & 0x00ff0000U ) | ( fourbytes << 24 );
#endif
}

void ivp_byte_swap2( ushort &twobytes )
{
#if defined( _WIN32 )
	twobytes = _byteswap_ushort( twobytes );
	// dimhotepus: Fast builtin byte swaps.
#elif defined( __clang__ ) || defined( __GNUC__ )
	twobytes = __builtin_bswap16( twobytes );
#else
	// dimhotepus: Well defined shifts instead of union punning.
	twobytes = static_cast< ushort >( ( twobytes >> 8 ) | ( twobytes << 8 ) );
#endif
}

// GEKKO / SUN__ branches removed: Win/Lin/Mac on x86, x86-64 and arm64 only.
void *p_malloc( std::size_t size )
{
	return std::malloc( size );
}

char *p_calloc( std::size_t nelem, std::size_t size )
{
	// dimhotepus: nelem * size could overflow and give a too small zeroed block.
	if ( size && nelem > SIZE_MAX / size )
		return nullptr;

	// dimhotepus: calloc gets pre-zeroed pages from the OS for big blocks, avoids malloc + memset.
	return static_cast< char * >( std::calloc( nelem, size ) );
}

void *p_realloc( void *memblock, std::size_t size )
{
	return std::realloc( memblock, size );
}

void p_free( void *data )
{
	std::free( data );
}

constexpr inline uint IVP_MEMORY_MAGIC{ 0x65981234U };

struct IVP_Aligned_Memory
{
	uint  magic_number;
	void *back_link;
};

// dimhotepus: Normalize alignment: must be a power of two and at least pointer size, because a void* is
// stored right before the returned block.
// Fix: the power-of-two requirement was only IVP_ASSERTed. In release a value such as 0x50 (the
// old IVU_MEM_ALIGN on 64-bit) produced a wrong mask: misaligned result and a back_link slot that
// could overlap the header. Round up to the next power of two instead.
[[nodiscard]] static constexpr inline std::size_t ivp_effective_alignment( unsigned short alignment )
{
	IVP_ASSERT( alignment != 0 && ( alignment & ( alignment - 1 ) ) == 0 );

	std::size_t a = alignment < alignof( void * ) ? alignof( void * ) : alignment;
	if ( a & ( a - 1 ) )
	{
		std::size_t p = 1;
		while ( p < a )
			p <<= 1;
		a = p;
	}
	return a;
}

// dimhotepus: ivp_malloc_aligned and ivp_calloc_aligned share this.
[[nodiscard]] static void *ivp_aligned_from_raw( void *raw, std::size_t alignment )
{
	static_cast< IVP_Aligned_Memory * >( raw )->magic_number = IVP_MEMORY_MAGIC;

	const std::uintptr_t ret =
	( reinterpret_cast< std::uintptr_t >( raw ) + alignment +
	  sizeof( IVP_Aligned_Memory ) - 1 ) &
	~( static_cast< std::uintptr_t >( alignment ) - 1 );

	reinterpret_cast< void ** >( ret )[-1] = raw;
	return reinterpret_cast< void * >( ret );
}

// dimhotepus: Total size with overflow check (size += ... could wrap and return a tiny block).
[[nodiscard]] static bool ivp_aligned_total( std::size_t size, std::size_t alignment, std::size_t &total )
{
	const std::size_t extra = alignment + sizeof( IVP_Aligned_Memory );
	if ( size > SIZE_MAX - extra )
		return false;
	total = size + extra;
	return true;
}

void *ivp_malloc_aligned( std::size_t size, unsigned short alignment )
{
	const std::size_t align = ivp_effective_alignment( alignment );
	std::size_t       total;
	if ( !ivp_aligned_total( size, align, total ) )
		return nullptr;

	void *data = p_malloc( total );
	return data ? ivp_aligned_from_raw( data, align ) : nullptr;
}

void *IVP_CDECL ivp_calloc_aligned( std::size_t size, unsigned short alignment )
{
	const std::size_t align = ivp_effective_alignment( alignment );
	std::size_t       total;
	if ( !ivp_aligned_total( size, align, total ) )
		return nullptr;

	void *data = p_calloc( 1, total );
	return data ? ivp_aligned_from_raw( data, align ) : nullptr;
}

void ivp_free_aligned( void *data )
{
	// dimhotepus: free( nullptr ) is legal, but data[-1] of nullptr crashed.
	if ( !data )
		return;

	auto *am = static_cast< IVP_Aligned_Memory * >( reinterpret_cast< void ** >( data )[-1] );

	IVP_ASSERT( am->magic_number == IVP_MEMORY_MAGIC );
	IVP_IF( 1 )
	{
		am->magic_number = 0;
	}
	p_free( am );
}

#if !defined( MEMTEST )
// dimhotepus: Usable bytes of the default first / regular block. Single constant: the first block and the
// block reset in free_mem_transaction() must agree, otherwise the allocator can hand out
// sizeof( p_Memory_Elem ) bytes beyond the block.
static constexpr std::size_t IVU_MEM_DEFAULT_USABLE =
	IVU_MEMORY_BLOCK_SIZE - sizeof( p_Memory_Elem );

[[nodiscard]] static constexpr inline std::uintptr_t ivu_align_up( std::uintptr_t address )
{
	return ( address + IVU_MEM_ALIGN - 1 ) & IVU_MEM_MASK;
}
#endif

IVP_U_Memory::~IVP_U_Memory()
{
	free_mem();
}

void IVP_U_Memory::init_mem_transaction_usage( char *external_mem, std::size_t size )
{
	transaction_in_use = 0;
#if defined( MEMTEST )
	(void)external_mem;
	(void)size;
#else
	size_of_external_mem = 0;

	bool use_external = false;
	if ( external_mem )
	{
		// dimhotepus: The alignment of the caller's buffer was only IVP_ASSERTed. In release a
		// misaligned buffer made the first store to memelem->next misaligned (UB, traps on
		// strict-alignment targets). Align the start up and give up the few skipped bytes.
		const auto elem_align = static_cast< std::uintptr_t >( alignof( p_Memory_Elem ) );
		const auto raw_base = reinterpret_cast< std::uintptr_t >( external_mem );

		if ( size <= UINTPTR_MAX - raw_base ) // base + size must not wrap
		{
			const auto end = raw_base + size;
			const auto base = ( raw_base + elem_align - 1 ) & ~( elem_align - 1 );

			if ( base < end && end - base >= offsetof( p_Memory_Elem, data ) )
			{
				const auto begin = ivu_align_up( base + offsetof( p_Memory_Elem, data ) );
				if ( begin < end )
				{
					auto *memelem = reinterpret_cast< p_Memory_Elem * >( base );
					memelem->next = last_elem;
					last_elem = memelem;

					size_of_external_mem = end - begin;
					speicherbeginn = reinterpret_cast< char * >( begin );
					speicherende = reinterpret_cast< char * >( end );
					use_external = true;
				}
			}
		}

		// dimhotepus: External memory is too small (or wraps the address space): fall back to a heap block.
		IVP_ASSERT( use_external );
	}

	if ( !use_external )
	{
		neuer_sp_block( 0 );
	}
	first_elem = last_elem;
#endif
}

void IVP_U_Memory::free_mem_transaction()
{
#if defined( MEMTEST )
	for ( int i = mem_vector.len() - 1; i >= 0; i-- )
	{
		ivp_free_aligned( mem_vector.element_at( i ) );
	}
	mem_vector.clear();
#else
	// dimhotepus: first_elem is null if init_mem_transaction_usage() was never called (or
	// the first block allocation failed). Remaining blocks are released by free_mem().
	if ( !first_elem )
		return;

	struct p_Memory_Elem *f, *n = nullptr;
	for ( f = last_elem; f; f = n )
	{
		n = f->next;
		if ( f == first_elem )
		{
			break;
		}
		p_free( f );
	}
	speicherbeginn =
	static_cast< char * >( align_to_next_adress( &first_elem->data[0] ) );

	// dimhotepus: See IVU_MEM_DEFAULT_USABLE, was IVU_MEMORY_BLOCK_SIZE = buffer overflow.
	const std::size_t ng =
	size_of_external_mem ? size_of_external_mem : IVU_MEM_DEFAULT_USABLE;

	speicherende = speicherbeginn + ng;
	last_elem = first_elem;
#endif
}

#if !defined( MEMTEST )
char *IVP_U_Memory::neuer_sp_block( std::size_t groesse )
{
	std::size_t ng = IVU_MEM_DEFAULT_USABLE;

	// dimhotepus: Rounding up and adding the header could overflow size_t for huge requests -> tiny
	// block returned as huge. Fail instead.
	if ( groesse > SIZE_MAX - ( IVU_MEM_ALIGN - 1 ) )
		return nullptr;
	groesse += IVU_MEM_ALIGN - 1;
	groesse &= IVU_MEM_MASK;

	const bool oversized = groesse > ng;
	if ( oversized )
		ng = groesse;

	if ( ng > SIZE_MAX - IVU_MEM_ELEM_OVERHEAD )
		return nullptr;

	// dimhotepus: Exactly header offset + alignment padding + payload.
	auto *memelem = static_cast< p_Memory_Elem * >( p_malloc( IVU_MEM_ELEM_OVERHEAD + ng ) );
	if ( !memelem )
		return nullptr;

	memelem->next = last_elem;
	last_elem = memelem;
	char *tmp = static_cast< char * >( align_to_next_adress( &memelem->data[0] ) );

	// dimhotepus: Oversized one-shot block does not replace the current bump block, otherwise the
	// free space left in the current block would be wasted after every big allocation. The block is
	// still in the chain so it is freed normally.
	if ( !oversized )
	{
		speicherbeginn = tmp + groesse;
		speicherende = tmp + ng;
	}
	return tmp;
}
#endif

void *IVP_U_Memory::get_memc( std::size_t groesse )
{
	void *neubeginn = get_mem( groesse );
	// dimhotepus: get_mem returns nullptr on OOM (or 0 size under MEMTEST), memset( nullptr ) is UB.
	if ( neubeginn )
		std::memset( neubeginn, 0, groesse );
	return neubeginn;
}

IVP_U_Memory::IVP_U_Memory()
{
	init_mem();
}

void IVP_U_Memory::init_mem()
{
	transaction_in_use = 3; // for transactions: first start init_mem_transaction_usage
	size_of_external_mem = 0;
#if !defined( MEMTEST )
	speicherbeginn = nullptr;
	speicherende = nullptr;
	last_elem = nullptr;
	first_elem = nullptr;
#endif
}

void IVP_U_Memory::free_mem()
{
#if defined( MEMTEST )
	for ( int i = mem_vector.len() - 1; i >= 0; i-- )
	{
		ivp_free_aligned( mem_vector.element_at( i ) );
	}
	mem_vector.clear();
#else
	struct p_Memory_Elem *f, *n;
	for ( f = last_elem; f; f = n )
	{
		n = f->next;
		if ( this->size_of_external_mem && f == first_elem )
		{
			break;
		}
		// dimhotepus: P_FREE macro -> p_free
		p_free( f );
	}
	speicherbeginn = nullptr;
	speicherende = nullptr;
	last_elem = nullptr;
	first_elem = nullptr;
	// dimhotepus: Stale external size made a later free_mem() think a heap first block is external.
	size_of_external_mem = 0;
#endif
}