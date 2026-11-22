#ifndef IVP_MEM_INCLUDED
#define IVP_MEM_INCLUDED

// dimhotepus: Need size_t, offsetof, uintptr_t;
#include <cstddef>
#include <cstdint>

// dimhotepus: Old 64-bit values were inconsistent: ALIGN=0x50 (80, not a power of two, comment said 64)
// while MASK aligned to 32. Stride was 80 but pointers aligned to 32 -> wasted memory, and under MEMTEST
// ivp_malloc_aligned(.., 0x50) produced garbage alignment. Use one power of two for all platforms
// (x86, x86-64, arm64; Win/Lin/Mac).
#ifdef PLATFORM_64BITS
constexpr inline unsigned IVU_MEM_ALIGN{ 0x40U }; // align to 64 bytes (power of two)
#else
constexpr inline unsigned IVU_MEM_ALIGN{ 0x20U }; // align to 32 bytes (power of two)
#endif
// dimhotepus: Derive mask from alignment (and uintptr_t width) instead of hardcoded 32/64-bit literals.
constexpr inline std::uintptr_t IVU_MEM_MASK{ ~( static_cast< std::uintptr_t >( IVU_MEM_ALIGN ) - 1U ) };
// dimhotepus: Same value on all platforms (was 0x7fe0 on both), single definition.
constexpr inline unsigned IVU_MEMORY_BLOCK_SIZE{ 0x8000U - IVU_MEM_ALIGN }; // size of block loaded by malloc

static_assert( ( IVU_MEM_ALIGN & ( IVU_MEM_ALIGN - 1U ) ) == 0,
               "IVU_MEM_ALIGN must be power of two." );

struct p_Memory_Elem
{
	struct p_Memory_Elem *next;
	char                  data[4]; //-V112
};

class IVP_U_Memory
{
#ifdef MEMTEST
	IVP_U_Vector< char > mem_vector;
#else
	p_Memory_Elem *first_elem;
	p_Memory_Elem *last_elem;
	char          *speicherbeginn;
	char          *speicherende;
#endif

	// dimhotepus: Was 'short : 16' (implementation-defined signedness bitfield) and
	// 'unsigned short : 16' which silently truncated external memory sizes > 64KB.
	int         transaction_in_use; // for asserts
	std::size_t size_of_external_mem; // usable bytes of external first block, or 0 if not external

	void        *speicher_callok( unsigned int groesse );
	void         free_mem_transaction();
	inline void *align_to_next_adress( void *p );

public:
	IVP_U_Memory();
	~IVP_U_Memory();
	void  init_mem();
	void *get_mem( size_t size );
	template< typename T >
	T *get_mem( size_t size )
	{
		// dimhotepus: Guard sizeof( T ) * size overflow (would return too small block).
		if ( size > SIZE_MAX / sizeof( T ) )
			return nullptr;
		return static_cast< T * >( get_mem( sizeof( T ) * size ) );
	}
	void *get_memc( size_t size );
	template< typename T >
	T *get_memc( size_t size )
	{
		// dimhotepus: Guard sizeof( T ) * size overflow.
		if ( size > SIZE_MAX / sizeof( T ) )
			return nullptr;
		return static_cast< T * >( get_memc( sizeof( T ) * size ) );
	}
	void free_mem();
#if !defined( MEMTEST )
	char *neuer_sp_block( size_t groesse );
#endif

	// for usage with transactions
	void init_mem_transaction_usage(
	char  *external_mem = 0,
	size_t size = 0 );                       // alloc first block, that block is never freed (except
											 // whole memory is destroyed)
	inline void end_memory_transaction();    // free all memory blocks except first
											 // one, reset memory pointer
	inline void  start_memory_transaction(); // only one transaction at a time
	inline void *get_mem_transaction( size_t size );
};

void IVP_U_Memory::start_memory_transaction()
{
	// IVP_ASSERT(transaction_in_use==0);
	IVP_IF( 1 )
	{
#ifdef SUN
		if ( size_of_external_mem == 0 )
		{
			init_mem_transaction_usage(); // get first block
		}
#endif
	}
	++transaction_in_use;
}

void IVP_U_Memory::end_memory_transaction()
{
	// dimhotepus: Check we have transaction.
	IVP_ASSERT( transaction_in_use > 0 );
	--transaction_in_use;

	free_mem_transaction();
	IVP_IF( 1 )
	{
#ifdef SUN
		free_mem(); // clear last block to be able to detect unitialized memory
					// access
#endif
	}
}

// warning: dependency with function neuer_sp_block
inline void *IVP_U_Memory::align_to_next_adress( void *p )
{
	// dimhotepus: Do math on uintptr_t, not pointers; mask is already uintptr_t wide.
	auto address = reinterpret_cast< std::uintptr_t >( p );
	address += IVU_MEM_ALIGN - 1;
	address &= IVU_MEM_MASK;
	return reinterpret_cast< void * >( address );
}

inline void *IVP_U_Memory::get_mem( size_t groesse )
{
#ifdef MEMTEST
	if ( groesse )
	{
		char *data = (char *)ivp_malloc_aligned( groesse, IVU_MEM_ALIGN );
		mem_vector.add( data );
		return data;
	}
	else
	{
		return NULL;
	}
#else
	char *op = speicherbeginn;

	// dimhotepus: Old code did 'op + groesse' first: huge size wrapped pointer (UB, passes
	// the end check, returns too small memory). Check against remaining bytes first, then align.
	// Semantics are the same as before (exact fit goes to the new block), and the dead debug
	// IVP_IF( address range ) { op++; op--; } was removed from this hot path.
	const auto avail = static_cast< size_t >( speicherende - op );
	if ( groesse < avail )
	{
		char *p = static_cast< char * >( align_to_next_adress( op + groesse ) );
		if ( p < speicherende )
		{
			speicherbeginn = p;
			return op;
		}
	}

	return this->neuer_sp_block( groesse );
#endif
}

inline void *IVP_U_Memory::get_mem_transaction( size_t groesse )
{
	IVP_ASSERT( transaction_in_use == 1 );
	return get_mem( groesse );
}

#endif
