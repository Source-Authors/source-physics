// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>

#include "ivu_memory.hxx"

#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <new>  // std::nothrow

#if !defined( __MWERKS__ ) || !defined( __POWERPC__ )
#ifdef OSX
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif
#endif

// IVP_Environment *ivp_global_env=NULL;

void ivp_memory_check( [[maybe_unused]] void *a ) {}

void ivp_byte_swap4( uint &fourbytes )
{
#ifdef _WIN32
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
#ifdef _WIN32
	twobytes = _byteswap_ushort( twobytes );
	// dimhotepus: Fast builtin byte swaps.
#elif defined( __clang__ ) || defined( __GNUC__ )
	twobytes = __builtin_bswap16( twobytes );
#else
	// dimhotepus: Well defined shifts instead of union punning.
	twobytes = static_cast< ushort >( ( twobytes >> 8 ) | ( twobytes << 8 ) );
#endif
}

void *p_malloc( size_t size )
{
#ifndef GEKKO
	return malloc( size );
#else
	// dimhotepus: Plain new throws; callers expect nullptr on OOM like malloc.
	return new ( std::nothrow ) char[size];
#endif
}

char *p_calloc( size_t nelem, size_t size )
{
	// dimhotepus: nelem * size could overflow and give too small zeroed block.
	if ( size && nelem > SIZE_MAX / size )
		return nullptr;

#ifndef GEKKO
	// dimhotepus: calloc gets pre-zeroed pages from OS for big blocks, avoids malloc + memset.
	return static_cast< char * >( calloc( nelem, size ) );
#else
	const size_t s = nelem * size;
	char        *sp = new ( std::nothrow ) char[s];
	if ( sp )
		memset( sp, 0, s );
	return sp;
#endif
}

void *p_realloc( void *memblock, size_t size )
{
#ifndef GEKKO
	return realloc( memblock, size );
#else
	IVP_ASSERT( 0 );
	return 0;
#endif
}

void p_free( void *data )
{
#ifndef GEKKO
	free( data );
#else
	delete[] static_cast< char * >( data );
#endif
}

constexpr inline uint IVP_MEMORY_MAGIC{ 0x65981234U };

struct IVP_Aligned_Memory
{
	uint  magic_number;
	void *back_link;
};

// dimhotepus: Normalize alignment: must be power of two, and at least pointer size, because we store a
// void* right before the returned block. With alignment 1..4 on 64-bit that slot was misaligned
// (UB; faults on strict-alignment targets).
[[nodiscard]] static inline size_t ivp_effective_alignment( unsigned short alignment )
{
	IVP_ASSERT( alignment != 0 && ( alignment & ( alignment - 1 ) ) == 0 );
	return alignment < alignof( void * ) ? alignof( void * ) : alignment;
}

#if !defined( SUN__ )
// dimhotepus: ivp_malloc_aligned and ivp_calloc_aligned had copy pasted code; share it.
[[nodiscard]] static void *ivp_aligned_from_raw( void *raw, size_t alignment )
{
	static_cast< IVP_Aligned_Memory * >( raw )->magic_number = IVP_MEMORY_MAGIC;

	const std::uintptr_t ret =
	( reinterpret_cast< std::uintptr_t >( raw ) + alignment +
	  sizeof( IVP_Aligned_Memory ) - 1 ) &
	~( static_cast< std::uintptr_t >( alignment ) - 1 );

	reinterpret_cast< void ** >( ret )[-1] = raw;
	return reinterpret_cast< void * >( ret );
}

// dimhotepus: Total size with overflow check (size += ... could wrap and return tiny block).
[[nodiscard]] static bool ivp_aligned_total( size_t size, size_t alignment, size_t &total )
{
	const size_t extra = alignment + sizeof( IVP_Aligned_Memory );
	if ( size > SIZE_MAX - extra )
		return false;
	total = size + extra;
	return true;
}
#endif

void *ivp_malloc_aligned( size_t size, unsigned short alignment )
{
#if defined( SUN__ )
	return memalign( alignment, size );
#else
	const size_t align = ivp_effective_alignment( alignment );
	size_t       total;
	if ( !ivp_aligned_total( size, align, total ) )
		return nullptr;

	void *data = p_malloc( total );
	return data ? ivp_aligned_from_raw( data, align ) : nullptr;
#endif
}

void *IVP_CDECL ivp_calloc_aligned( size_t size, unsigned short alignment )
{
#if defined( SUN__ )
	// dimhotepus: Was no SUN__ branch: memory had header but ivp_free_aligned on SUN__ does plain
	// free() -> heap corruption. Keep both sides consistent.
	void *data = memalign( alignment, size );
	if ( data )
		memset( data, 0, size );
	return data;
#else
	const size_t align = ivp_effective_alignment( alignment );
	size_t       total;
	if ( !ivp_aligned_total( size, align, total ) )
		return nullptr;

	void *data = p_calloc( 1, total );
	return data ? ivp_aligned_from_raw( data, align ) : nullptr;
#endif
}

void ivp_free_aligned( void *data )
{
#if defined( SUN__ )
	p_free( data );
#else
	// dimhotepus: free( nullptr ) is legal, but data[-1] of nullptr crashed.
	if ( !data )
		return;

	auto *am = static_cast< IVP_Aligned_Memory * >( ( (void **)data )[-1] );

	IVP_ASSERT( am->magic_number == IVP_MEMORY_MAGIC );
	IVP_IF( 1 )
	{
		am->magic_number = 0;
	}
	p_free( am );
#endif
}

#if !defined( MEMTEST )
// dimhotepus: Usable bytes of the default first/regular block. Was computed in two places differently
// (neuer_sp_block used BLOCK - sizeof( elem ), block reset used BLOCK), so after
// end_memory_transaction() allocator could hand out sizeof( p_Memory_Elem ) bytes beyond the block
// -> heap buffer overflow. Single constant now.
static constexpr size_t IVU_MEM_DEFAULT_USABLE =
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

void IVP_U_Memory::init_mem_transaction_usage( char *external_mem, size_t size )
{
	// IVP_IF(1) {
	transaction_in_use = 0;
	//}
#if defined( MEMTEST )
#else
	size_of_external_mem = 0;

	if ( external_mem )
	{
		IVP_ASSERT( reinterpret_cast< std::uintptr_t >( external_mem ) % alignof( p_Memory_Elem ) == 0 );

		// dimhotepus: Old: size - IVU_MEM_ALIGN underflowed for small size, and end could be
		// up to sizeof( void * ) - 1 bytes past external memory (header offset was not accounted).
		// Compute the real aligned range in integers.
		const auto base = reinterpret_cast< std::uintptr_t >( external_mem );
		const auto end = base + size;
		const auto begin = ivu_align_up( base + offsetof( p_Memory_Elem, data ) );

		if ( size >= offsetof( p_Memory_Elem, data ) && begin < end )
		{
			auto *memelem = reinterpret_cast< p_Memory_Elem * >( external_mem );
			memelem->next = last_elem;
			last_elem = memelem;

			size_of_external_mem = end - begin;
			speicherbeginn = reinterpret_cast< char * >( begin );
			speicherende = reinterpret_cast< char * >( end );
		}
		else
		{
			// dimhotepus: External memory is too small to use, fall back to heap block.
			IVP_ASSERT( 0 );
			neuer_sp_block( 0 );
		}
	}
	else
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
	// IVP_ASSERT(first_elem!=NULL); playstation doesn't like this ...
	// dimhotepus: first_elem is null if init_mem_transaction_usage() was never called (or first
	// block allocation failed) -> first_elem->data below was null dereference. Remaining blocks
	// are released by free_mem().
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
	const size_t ng =
	size_of_external_mem ? size_of_external_mem : IVU_MEM_DEFAULT_USABLE;

	speicherende = speicherbeginn + ng;
	last_elem = first_elem;
#endif
}

#if !defined( MEMTEST )
char *IVP_U_Memory::neuer_sp_block( size_t groesse )
{
	size_t ng = IVU_MEM_DEFAULT_USABLE;

	// dimhotepus: Rounding up and adding header could overflow size_t for huge requests -> tiny
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

	// dimhotepus: Exactly header offset + alignment padding + payload (was sizeof + ALIGN, which
	// with the odd 0x50 alignment over-allocated and was fragile).
	auto *memelem = static_cast< p_Memory_Elem * >( p_malloc( IVU_MEM_ELEM_OVERHEAD + ng ) );
	if ( !memelem )
		return nullptr;

	memelem->next = last_elem;
	last_elem = memelem;
	char *tmp = static_cast< char * >( align_to_next_adress( &memelem->data[0] ) );

	// dimhotepus: Oversized one-shot block no longer replaces the current bump block, otherwise the
	// free space left in the current block was wasted after every big allocation. The block is
	// still in the chain so it is freed normally.
	if ( !oversized )
	{
		speicherbeginn = tmp + groesse;
		speicherende = tmp + ng;
	}
	return tmp;
}
#endif

void *IVP_U_Memory::get_memc( size_t groesse )
{
	//	if (groesse & 0x7) *(int *)0 = 0;
	void *neubeginn = get_mem( groesse );
	// dimhotepus: get_mem returns nullptr on OOM (or 0 size under MEMTEST), memset( nullptr ) is UB/crash.
	if ( neubeginn )
		memset( neubeginn, 0, groesse );
	return neubeginn;
}

IVP_U_Memory::IVP_U_Memory()
{
	init_mem();
}

void IVP_U_Memory::init_mem()
{
	transaction_in_use =
	3; // for transactions: first start init_mem_transaction_usage
	size_of_external_mem = 0;
#if defined( MEMTEST )
#else
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
		// dimhotepus: P_FREE macro -> p_free, consistent with the rest of file and GEKKO delete[].
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