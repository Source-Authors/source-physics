// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PUBLIC

#ifndef _IVP_BIGVECTOR_INCLUDED
#define _IVP_BIGVECTOR_INCLUDED

/********************************************************************************
 *	File:          ivu_bigvector.hxx
 *	Description:   A vector is an array of pointers
 *                 the length of this array is dynamically increased when
 *                 elements are inserted
 ********************************************************************************/

#include <cstddef>  // std::memmove
#include <cstring>
#include <utility>  // std::swap

// dimhotepus: Branch hint for cold grow path. __builtin_expect exists only on
// GCC/Clang (Linux, macOS, clang-cl), MSVC gets a no-op to stay portable.
#if defined( __GNUC__ ) || defined( __clang__ )
#define IVP_BIGVECTOR_UNLIKELY( x ) __builtin_expect( !!( x ), 0 )
#else
#define IVP_BIGVECTOR_UNLIKELY( x ) ( x )
#endif

struct IVP_U_BigVector_Base
{
	int    memsize{ 0 };
	int    n_elems{ 0 };
	void **elems{ nullptr };

	// dimhotepus: Out of line on purpose, keeps the hot add() path small
	// and inlinable.
	void increment_mem();

protected:
	// dimhotepus: Single place for the "buffer lives right behind the object"
	// check. It was duplicated in clear() and increment_mem(), easy to get out of sync.
	// True when elems points to the preallocated buffer, which must never be freed.
	bool is_inline_storage() const
	{
		return elems == reinterpret_cast<void *const *>( this + 1 );
	}
};

template< class T >
class IVP_U_BigVector : public IVP_U_BigVector_Base
{
	void ensure_capacity()
	{
		// dimhotepus: Growing is rare, tell the compiler so the fast path
		// is laid out as fall-through.
		if ( IVP_BIGVECTOR_UNLIKELY( n_elems >= memsize ) )
		{
			this->increment_mem();
		}
	}

protected:
	//  special vector with preallocated elems
	IVP_U_BigVector( void **ielems, int size )
	{
		// dimhotepus: The this + 1 trick is valid only if derived class adds
		// nothing to the base layout. Catch layout drift at compile time on
		// all targets (x86, x86-64, arm64) instead of corrupting memory at runtime.
		static_assert( sizeof( IVP_U_BigVector<T> ) == sizeof( IVP_U_BigVector_Base ),
					   "Preallocated storage layout relies on no extra members" );
		IVP_ASSERT( ielems == (void **)( this + 1 ) );
		// dimhotepus: Zero-sized inline buffer makes no sense and would
		// make memsize * 2 + 1 growth start from a bogus state.
		IVP_ASSERT( size > 0 );
		elems = ielems;
		memsize = size;
		n_elems = 0;
	}

public:
	// dimhotepus: Add explicit. Prevents silent int -> vector conversions.
	explicit IVP_U_BigVector( int size = 0 )
	{
		// dimhotepus: Negative size converts to a huge size_t in malloc
		// argument. Catch it in debug builds.
		IVP_ASSERT( size >= 0 );
		memsize = size;
		n_elems = 0;
		// dimhotepus: Cast to size_t before multiply, so size * sizeof does
		// not overflow signed int on 64-bit and has a defined type on 32-bit.
		elems = size > 0 ? (void **)p_malloc( (size_t)size * sizeof( void * ) ) : nullptr;
	}

	// dimhotepus: Class owns raw memory (or points into derived object),
	// implicit copy would cause double free / dangling inline buffer pointer.
	IVP_U_BigVector( const IVP_U_BigVector & ) = delete;
	IVP_U_BigVector &operator=( const IVP_U_BigVector & ) = delete;

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

	void exchange_vector_elems( int first, int second )
	{
		// dimhotepus: Bounds checks, out of range swap silently corrupted
		// neighbouring memory.
		IVP_ASSERT( first >= 0 && first < n_elems );
		IVP_ASSERT( second >= 0 && second < n_elems );
		// dimhotepus: std::swap instead of manual temp, same codegen, less code.
		std::swap( elems[first], elems[second] );
	}

	void remove_all() { n_elems = 0; }

	~IVP_U_BigVector() { this->clear(); }

	int len() const { return n_elems; }

	// dimhotepus: Make const (does not modify state) and take const T *, so it
	// can be called from const methods and with const pointers.
	// Returns index or -1. Searches from the back.
	int index_of( const T *elem ) const
	{
		// dimhotepus: Early return instead of break + shared exit. Declares
		// i in loop scope, result is the same (-1 if not found).
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
		IVP_ASSERT( index_of( elem ) == -1 );
		elems[n_elems] = (void *)elem;
		return n_elems++;
	}

	// dimhotepus: Keeps element order, O(n).
	void remove_at( int index )
	{
		IVP_ASSERT( ( index >= 0 ) && ( index < n_elems ) );
		// dimhotepus: One memmove instead of element-by-element loop, the
		// libc version uses SIMD / rep movs and is much faster for big tails.
		const int tail = n_elems - index - 1;
		if ( tail > 0 )
		{
			std::memmove( elems + index, elems + index + 1, (size_t)tail * sizeof( void * ) );
		}
		n_elems--;
	}

	// dimhotepus: New O(1) removal for callers which do not care about order
	// (swap with last). Purely additive, existing behavior is unchanged.
	void remove_at_unordered( int index )
	{
		IVP_ASSERT( ( index >= 0 ) && ( index < n_elems ) );
		elems[index] = elems[--n_elems];
	}

	int install( T *elem )
	{
		// dimhotepus: const, value is never modified.
		const int position = index_of( elem );
		if ( position >= 0 )
			return position;
		ensure_capacity();
		elems[n_elems] = (void *)elem;
		return n_elems++;
	}

	void remove( T *elem )
	{
		const int index = this->index_of( elem );
		IVP_ASSERT( index >= 0 );
		// dimhotepus: IVP_ASSERT is compiled out in release. With index == -1
		// the old code did n_elems-- and wrote elems[-1] (heap corruption).
		if ( index < 0 )
			return;
		// dimhotepus: Reuse remove_at (memmove) instead of duplicated loop.
		// Old loop also used the obscure ( elems + 1 )[index] form.
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
class IVP_U_BigVector_Enumerator
{
	int index;

public:
	// dimhotepus: Add explicit.
	explicit IVP_U_BigVector_Enumerator( IVP_U_BigVector< T > * /*vec*/ )
		: index( 0 )
	{
	}

	T *get_next_element( IVP_U_BigVector< T > *vec )
	{
		if ( index >= vec->n_elems )
			return nullptr;
		// dimhotepus: Fix enumeration [0..length).
		return vec->element_at( index++ );
	}
};

#endif