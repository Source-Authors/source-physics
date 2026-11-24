// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PUBLIC

#ifndef IVP_VECTOR_INCLUDED
#define IVP_VECTOR_INCLUDED

/********************************************************************************
 *	Class:         IVP_U_Vector_Base
 *	Description:   A vector is an array of pointers
 *                 the length of this array is dynamically increased when
 *                 elements are inserted
 ********************************************************************************/

#include <algorithm> // std::swap, std::reverse
#include <cstddef>
#include <cstring>  // std::memcpy, std::memmove
#include <utility>

// dimhotepus: Branch hint for the cold grow path. __builtin_expect exists only
// on GCC/Clang (Linux, macOS, clang-cl), MSVC gets a no-op to stay portable.
#if defined( __GNUC__ ) || defined( __clang__ )
#define IVP_VECTOR_UNLIKELY( x ) __builtin_expect( !!( x ), 0 )
#else
#define IVP_VECTOR_UNLIKELY( x ) ( x )
#endif

struct IVP_U_Vector_Base
{
	unsigned short memsize{ 0 };
	unsigned short n_elems{ 0 };
	void         **elems{ nullptr };

	// dimhotepus: Out of line on purpose, keeps the hot add() path small.
	void increment_mem();

protected:
	// dimhotepus: Single place for the "buffer lives right behind the object"
	// check. It was open-coded 4 times (clear, uses_inline_buffer,
	// reserve_discarding, increment_mem), easy to get out of sync.
	// True when elems points to the preallocated buffer, which must never be freed.
	bool is_inline_storage() const
	{
		return elems == reinterpret_cast< void *const * >( this + 1 );
	}
};

template< class T >
class IVP_U_Vector : public IVP_U_Vector_Base
{
private:
	// Ensures capacity for at least 'count' elements. Does NOT preserve the
	// current contents, so only use it when they are about to be overwritten.
	void reserve_discarding( int count )
	{
		IVP_ASSERT( count >= 0 && count <= 0xFFFF );
		if ( count <= memsize )
			return;

		// dimhotepus: Cast to size_t before multiply, defined type on 32-bit
		// (x86) and 64-bit (x86-64, arm64) targets.
		void **fresh = (void **)p_malloc( (size_t)count * sizeof( void * ) );

		// Never free the inline buffer of a pre-allocated vector.
		// dimhotepus: Also skip nullptr, empty vectors have no buffer to free.
		if ( elems && !is_inline_storage() )
		{
			void *old = (void *)elems;
			p_free( old );
		}
		elems = fresh;
		memsize = (unsigned short)count;
	}

	// dimhotepus: Copy pointers from other, shared by copy and move paths.
	// Was duplicated 4 times. Capacity is reserved without preserving contents.
	void assign_from( const IVP_U_Vector &other )
	{
		reserve_discarding( other.n_elems );
		if ( other.n_elems )
		{
			std::memcpy( elems, other.elems, (size_t)other.n_elems * sizeof( void * ) );
		}
		n_elems = other.n_elems;
	}

	// dimhotepus: Steal heap storage of other, shared by move ctor and move assign.
	void steal_heap_storage( IVP_U_Vector &other )
	{
		elems = other.elems;
		memsize = other.memsize;
		n_elems = other.n_elems;

		other.elems = nullptr;
		other.memsize = 0;
		other.n_elems = 0;
	}

public:
	// dimhotepus: Add copy.
	// Copy is always heap-backed and sized exactly to the source.
	IVP_U_Vector( const IVP_U_Vector &other ) :
		IVP_U_Vector_Base()
	{
		assign_from( other );
	}

	// dimhotepus: Add copy.
	IVP_U_Vector &operator=( const IVP_U_Vector &other )
	{
		if ( this == &other )
			return *this;

		assign_from( other );
		return *this;
	}

	// dimhotepus: Add move.
	IVP_U_Vector( IVP_U_Vector &&other ) noexcept :
		IVP_U_Vector_Base()
	{
		if ( other.is_inline_storage() )
		{
			// Can't steal storage that lives inside 'other': copy the pointers.
			assign_from( other );
			other.n_elems = 0; // keeps its inline buffer, now empty
		}
		else
		{
			steal_heap_storage( other );
		}
	}

	// dimhotepus: Add move.
	IVP_U_Vector &operator=( IVP_U_Vector &&other ) noexcept
	{
		if ( this == &other )
			return *this;

		if ( other.is_inline_storage() )
		{
			// Copy into our own storage (inline or heap), then empty the source.
			assign_from( other );
			other.n_elems = 0;
		}
		else
		{
			// Drop our heap buffer (if any), then take the source's.
			// dimhotepus: clear() does exactly what the removed
			// release_heap_buffer() did, one copy of the logic is enough.
			clear();
			steal_heap_storage( other );
		}
		return *this;
	}

	void ensure_capacity()
	{
		// dimhotepus: Growing is rare, tell the compiler so the fast path
		// is laid out as fall-through.
		if ( IVP_VECTOR_UNLIKELY( n_elems >= memsize ) )
		{
			this->increment_mem();
		}
	}

protected:
	//  special vector with preallocated elems
	IVP_U_Vector( void **ielems, int size )
	{
		// dimhotepus: The this + 1 trick is valid only if derived class adds
		// nothing to the base layout. Catch layout drift at compile time on
		// all targets instead of corrupting memory at runtime.
		static_assert( sizeof( IVP_U_Vector< T > ) == sizeof( IVP_U_Vector_Base ),
		               "Preallocated storage layout relies on no extra members" );
		IVP_ASSERT( ielems == (void **)( this + 1 ) );
		IVP_ASSERT( size >= 0 && size <= 0xFFFF );
		elems = ielems;
		memsize = (unsigned short)size;
		n_elems = 0;
	}

public:
	// dimhotepus: Add explicit. Prevents silent int -> vector conversions.
	explicit IVP_U_Vector( int size = 0 )
	{
		IVP_ASSERT( size >= 0 && size <= 0xFFFF );
		memsize = (unsigned short)size;
		n_elems = 0;
		// dimhotepus: Allocate from the stored memsize, not from 'size'. In
		// release the two differed for size > 0xFFFF (truncation), leaving
		// memsize == 0 with a live allocation.
		elems = memsize ? (void **)p_malloc( (size_t)memsize * sizeof( void * ) ) : nullptr;
	}

	void clear()
	{
		// dimhotepus: Use shared helper instead of open-coded this + 1 compare.
		if ( !is_inline_storage() )
		{
			void *dummy = (void *)elems;
			P_FREE( dummy );
			// dimhotepus: Use nullptr instead of 0 for pointer.
			elems = nullptr;
			memsize = 0;
		}
		n_elems = 0;
	}

	void remove_all() { n_elems = 0; }

	~IVP_U_Vector() { this->clear(); }

	int len() const { return n_elems; }

	// dimhotepus: Make const (does not modify state) and take const T *, so it
	// can be called from const methods and with const pointers.
	// Returns index or -1. Searches from the back.
	int index_of( const T *elem ) const
	{
		for ( int i = n_elems - 1; i >= 0; i-- )
		{
			if ( elems[i] == elem )
				return i;
		}
		return -1;
	}

	int add( T *elem )
	{
		ensure_capacity();
		// IVP_ASSERT( index_of(elem) == -1);
		elems[n_elems] = (void *)elem;
		return n_elems++;
	}

	int install( T *elem )
	{
		// dimhotepus: const, value is never modified.
		const int old_index = index_of( elem );
		if ( old_index != -1 )
			return old_index;
		ensure_capacity();
		elems[n_elems] = (void *)elem;
		return n_elems++;
	}

	void swap_elems( int index1, int index2 )
	{
		IVP_ASSERT( ( index1 >= 0 ) && ( index1 < n_elems ) );
		IVP_ASSERT( ( index2 >= 0 ) && ( index2 < n_elems ) );
		// dimhotepus: std::swap instead of manual temp, same codegen. Removed
		// useless trailing return.
		std::swap( elems[index1], elems[index2] );
	}

	void insert_after( int index, T *elem )
	{
		IVP_ASSERT( ( index >= 0 ) && ( index < n_elems ) );
		index++;
		ensure_capacity();
		// dimhotepus: One memmove instead of element-by-element shift, libc
		// version uses SIMD and is much faster for long tails. Skipped when
		// appending at the end (nothing to move).
		const int tail = n_elems - index;
		if ( tail > 0 )
		{
			std::memmove( elems + index + 1, elems + index, (size_t)tail * sizeof( void * ) );
		}
		elems[index] = (void *)elem;
		n_elems++;
	}

	// dimhotepus: Keeps element order, O(n).
	void remove_at( int index )
	{
		IVP_ASSERT( ( index >= 0 ) && ( index < n_elems ) );
		// dimhotepus: One memmove instead of element-by-element loop.
		const int tail = n_elems - index - 1;
		if ( tail > 0 )
		{
			std::memmove( elems + index, elems + index + 1, (size_t)tail * sizeof( void * ) );
		}
		n_elems--;
	}

	void reverse()
	{
		// dimhotepus: std::reverse instead of n/2 calls to swap_elems with
		// their per-call asserts. Safe for empty vectors (nullptr, nullptr).
		std::reverse( elems, elems + n_elems );
	}

	// dimhotepus: O(1), does NOT keep order.
	void remove_at_and_allow_resort( int index )
	{
		IVP_ASSERT( ( index >= 0 ) && ( index < n_elems ) );
		n_elems--;
		elems[index] = elems[n_elems];
	}

	void remove_allow_resort( T *elem )
	{
		const int index = this->index_of( elem );
		IVP_ASSERT( index >= 0 );
		// dimhotepus: Prevent out of bounds in Release.
		if ( index < 0 )
			return;

		n_elems--;
		elems[index] = elems[n_elems];
	}

	void remove( T *elem )
	{
		const int index = this->index_of( elem );
		IVP_ASSERT( index >= 0 );
		// dimhotepus: Prevent out of bounds in Release.
		if ( index < 0 )
			return;

		// dimhotepus: Reuse remove_at (memmove) instead of a second copy of it.
		remove_at( index );
	}

	T *element_at( int index ) const
	{
		IVP_ASSERT( index >= 0 && index < n_elems );
		return (T *)elems[index];
	}
};

// For threadsave usage
template< class T >
class IVP_U_Vector_Enumerator
{
	int index;

public:
	// dimhotepus: Add explicit.
	explicit IVP_U_Vector_Enumerator( IVP_U_Vector< T > * /*vec*/ ) :
		index( 0 )
	{
	}

	T *get_next_element( IVP_U_Vector< T > *vec )
	{
		if ( index >= vec->n_elems )
			return nullptr;
		// dimhotepus: Fix enumeration [0..length).
		return vec->element_at( index++ );
	}
};

// vector of cores with 2 default elements already alloced
class IVP_Vector_of_Cores_2 : public IVP_U_Vector< class IVP_Core >
{
	// dimhotepus: void * instead of IVP_Core *. The slots are accessed through
	// void ** (elems), reading IVP_Core * objects through void * lvalues violates
	// strict aliasing. Same size and alignment, so the this + 1 layout is unchanged.
	void *elem_buffer[2];

public:
	IVP_Vector_of_Cores_2() :
		IVP_U_Vector< IVP_Core >( (void **)&elem_buffer[0], 2 )
	{
		elem_buffer[0] = nullptr;
		elem_buffer[1] = nullptr;
	}
};

#endif
