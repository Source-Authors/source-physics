// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>
#include "ivu_min_hash.hxx"

#include <cstddef>
#include <cstdint>

namespace
{
// dimhotepus: Single "is a smaller than b" comparison used by every min
// search. Before, add() / min_added_at_index() compared exactly (< and >=)
// while removal used almost_equal + cmp_index, so insert and remove could
// disagree about which element is the minimum for nearly-equal values.
// Exact ties behave as before: a new element has the highest cmp_index, so it
// never displaces an equal-valued minimum.
inline bool ivp_min_hash_less( const IVP_U_Min_Hash_Elem *a, const IVP_U_Min_Hash_Elem *b )
{
#if defined( SORT_MINDIST_ELEMENTS )
	return a->value < b->value ||
	       ( hk_Math::almost_equal( a->value, b->value ) && a->cmp_index < b->cmp_index );
#else
	return a->value < b->value;
#endif
}
} // namespace

IVP_U_Min_Hash::IVP_U_Min_Hash( int sizei )
{
	// dimhotepus: The old loop tested (1 << i) up to i == 31, which is signed
	// overflow (UB), and accepted sizei <= 0 in release builds. Use the
	// power-of-two bit trick and cap the size so size * 2 can't overflow.
	IVP_ASSERT( sizei > 0 && sizei <= ( 1 << 30 ) && ( sizei & ( sizei - 1 ) ) == 0 );

	size = static_cast< unsigned int >( sizei );
	// dimhotepus: Do the size math in size_t. int arithmetic could overflow on
	// large sizes before the widening conversion.
	const std::size_t n = static_cast< std::size_t >( size );

	elems = reinterpret_cast< IVP_U_Min_Hash_Elem ** >( p_calloc( sizeof( void * ), n ) );
	stadel = reinterpret_cast< IVP_U_Min_Hash_Elem ** >( p_calloc( sizeof( void * ), n * 2 ) );
	IVP_ASSERT( elems != nullptr && stadel != nullptr );

	min_per_array_pos = stadel + n;
	counter = 0;
#if defined( SORT_MINDIST_ELEMENTS )
	next_cmp_index = 1;
#endif
}

IVP_U_Min_Hash::~IVP_U_Min_Hash()
{
	// dimhotepus: Dropped the "if ( !elems[i] ) continue;" pre-check. The inner
	// loop already handles an empty list.
	for ( unsigned int i = 0; i < size; i++ )
	{
		IVP_U_Min_Hash_Elem *next_elem;
		for ( IVP_U_Min_Hash_Elem *elem = elems[i]; elem; elem = next_elem )
		{
			next_elem = elem->next;
			delete elem;
		}
	}
	P_FREE( elems );
	P_FREE( stadel );
}

/**
 *	elems:			array of unsorted linked lists
 *	min_per_array_pos:	parallel array of minimal elements per linked list
 *
 *	stadel:		binary tree of minimal elements
 *			stadel[1] == root
 *			stadel[2-3] next level
 *			stadel[4-7] ""
 *			...
 *			stadel[size/2 - size-1]
 *			stadel[size - 2size-1] == min_per_array_pos[0 .. size-1]
 *			(same memory)
 */

// a new minimum element at a given hash index was set, so update stadel
void IVP_U_Min_Hash::min_added_at_index( IVP_U_Min_Hash_Elem *elem, hk_intp i )
{
	// dimhotepus: Explicit cast. hk_intp += unsigned silently converted to
	// unsigned on 32-bit targets.
	i += static_cast< hk_intp >( size );
	stadel[i] = elem; // transport new min element from bottom to top till
	                  // smaller element is found
	for ( unsigned height = size >> 1; height > 0; height >>= 1 )
	{
		i = i >> 1;
		// dimhotepus: Was "val >= stadel[i]->value" (exact). Same comparison as
		// removal now, see ivp_min_hash_less.
		if ( stadel[i] && !ivp_min_hash_less( elem, stadel[i] ) )
		{
			break;
		}
		stadel[i] = elem;
	}
}

// the minimum element at an array position has been removed
// search new minimum in linked list.
void IVP_U_Min_Hash::min_removed_at_index( IVP_U_Min_Hash_Elem *elem, hk_intp i )
{
	{ // search hash index position for new minimal element
		IVP_U_Min_Hash_Elem *new_min = elems[i];
		if ( new_min )
		{
			for ( IVP_U_Min_Hash_Elem *el = new_min->next; el; el = el->next )
			{
				if ( ivp_min_hash_less( el, new_min ) )
				{
					new_min = el;
				}
			}
		}
		min_per_array_pos[i] = new_min;
	}

	// go from bottom to top and trace path of elem
	// as long as element is found update stadel
	// dimhotepus: Explicit cast, see min_added_at_index.
	i += static_cast< hk_intp >( size );
	for ( unsigned height = size >> 1; height > 0; height >>= 1 )
	{
		i = i >> 1;
		if ( elem != stadel[i] )
		{ // finito
			break;
		}
		const hk_intp j = i * 2;
		if ( !stadel[j] )
		{
			stadel[i] = stadel[j + 1];
		}
		else if ( !stadel[j + 1] )
		{
			stadel[i] = stadel[j];
		}
		else
		{
			stadel[i] = ivp_min_hash_less( stadel[j], stadel[j + 1] ) ? stadel[j] : stadel[j + 1];
		}
	}
}

void IVP_U_Min_Hash::add( void *elem, IVP_DOUBLE val )
{
	// dimhotepus: Pass the pointer value, not its address. See hash_index in
	// the header (strict aliasing).
	const hk_intp        i = hash_index( elem );
	IVP_U_Min_Hash_Elem *el = new IVP_U_Min_Hash_Elem();
#if defined( SORT_MINDIST_ELEMENTS )
	// dimhotepus: Per-instance counter instead of a function-local static
	// shared across all hashes (data race, and signed overflow after 2^31 adds).
	// Only the order within one hash matters, so behaviour is the same.
	el->cmp_index = next_cmp_index++;
#endif
	el->elem = elem;
	el->next = elems[i];
	elems[i] = el;
	el->value = val;
	counter++;
	// dimhotepus: Was "val < min_per_array_pos[i]->value" (exact). See
	// ivp_min_hash_less.
	if ( !min_per_array_pos[i] || ivp_min_hash_less( el, min_per_array_pos[i] ) )
	{
		min_added_at_index( el, i );
	}
}

/** update element in min_hash */
void IVP_U_Min_Hash::change_value( void *elem, IVP_DOUBLE val )
{
	remove( elem );
	add( elem, val );
}

/** try to remove element from min_hash */
void IVP_U_Min_Hash::remove( void *elem )
{
	const hk_intp        i = hash_index( elem ); // dimhotepus: see add()
	IVP_U_Min_Hash_Elem *last_el = nullptr;
	for ( IVP_U_Min_Hash_Elem *el = elems[i]; el; el = el->next )
	{
		if ( el->elem == elem )
		{
			if ( last_el )
				last_el->next = el->next;
			else
				elems[i] = el->next;

			if ( el == min_per_array_pos[i] )
				min_removed_at_index( el, i );

			P_DELETE( el );
			counter--;
			return;
		}
		last_el = el;
	}
}

bool IVP_U_Min_Hash::is_elem( void *elem ) const
{
	const hk_intp i = hash_index( elem ); // dimhotepus: see add()
	for ( const IVP_U_Min_Hash_Elem *el = elems[i]; el; el = el->next )
	{
		if ( el->elem == elem )
			return true;
	}
	return false;
}

void IVP_U_Min_Hash::remove_min()
{
	// dimhotepus: The old code cast the user's void* to IVP_U_Min_Hash_Elem* and
	// passed that to remove(). It only worked because remove() takes void*, and
	// the cast was misleading. Pass the user pointer as-is.
	void *min = find_min_elem();
	if ( min )
	{
		remove( min );
	}
}