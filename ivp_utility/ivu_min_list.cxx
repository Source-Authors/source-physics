// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>
#include "ivu_min_list.hxx"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

IVP_U_Min_List::IVP_U_Min_List( int start_size )
{
	// dimhotepus: The old code only clamped the upper bound. start_size <= 0
	// made elems[malloced_size - 1] below underflow. Clamp to [1, MAX]. The old
	// comment claimed an assertion here, but it was a silent clamp.
	unsigned int initial = start_size < 1 ? 1u : static_cast< unsigned int >( start_size );
	if ( initial > IVP_U_MINLIST_MAX_ALLOCATION )
		initial = IVP_U_MINLIST_MAX_ALLOCATION;

	malloced_size = initial;
	counter = 0;
	elems = static_cast< IVP_U_Min_List_Element * >(
		p_malloc( sizeof( IVP_U_Min_List_Element ) * malloced_size ) );
	IVP_ASSERT( elems != nullptr );

	unsigned int i = 0;
	free_list = i;
	for ( ; i < malloced_size; i++ )
	{
		elems[i].next = i + 1;
	}

	elems[malloced_size - 1].next = IVP_U_MINLIST_UNUSED;
	first_element = IVP_U_MINLIST_UNUSED;

#ifdef IVP_U_MINLIST_USELONG
	first_long = IVP_U_MINLIST_UNUSED;
#endif
	min_value = IVP_U_MINLIST_MAXVALUE;
}

IVP_U_Min_List::~IVP_U_Min_List()
{
	P_FREE( elems );
}

IVP_U_MINLIST_INDEX IVP_U_Min_List::add( void                     *elem,
                                         IVP_U_MINLIST_FIXED_POINT value )
{
	// search free element first
	IVP_ASSERT( value <= P_FLOAT_MAX );
	IVP_U_Min_List_Element *e;
	IVP_U_MINLIST_INDEX     return_index;
	counter += 1;

	if ( free_list != IVP_U_MINLIST_UNUSED )
	{
		IVP_ASSERT( free_list < malloced_size );

		return_index = free_list;
		e = &elems[free_list];
		free_list = e->next;
	}
	else
	{
		// dimhotepus: The old code only asserted here. In release builds, at
		// capacity, the clamped "new" size equaled the old one, return_index
		// became malloced_size (out of range) and the last live element's next
		// link was overwritten. Fail loudly instead of corrupting the heap.
		if ( malloced_size >= IVP_U_MINLIST_MAX_ALLOCATION )
		{
			std::fprintf( stderr, "IVP_U_Min_List: out of capacity (%u elements)\n",
			              malloced_size );
			IVP_ASSERT( false );
			std::abort();
		}

		// Clamp allocation to IVP_U_MINLIST_MAX_ALLOCATION
		// dimhotepus: int -> unsigned int.
		unsigned int nNewMallocSize = malloced_size * 2 + 1;
		if ( nNewMallocSize > IVP_U_MINLIST_MAX_ALLOCATION )
			nNewMallocSize = IVP_U_MINLIST_MAX_ALLOCATION;

#ifndef NDEBUG
		// dimhotepus: int -> unsigned int.
		static unsigned int g_ErrorCount = 0;
		if ( g_ErrorCount < 3u )
		{
			if ( nNewMallocSize > 2000u )
			{
				ivp_message(
				"Warning! Large min_list array size (%u)! Could indicate long "
				"sweeps\n",
				nNewMallocSize );
				++g_ErrorCount;
			}
		}
#endif

		// dimhotepus: Allocate exactly nNewMallocSize. The old "+ 1" spare
		// element only hid the out-of-range return_index at capacity.
		IVP_U_Min_List_Element *new_elems = static_cast< IVP_U_Min_List_Element * >(
		p_malloc( sizeof( IVP_U_Min_List_Element ) * nNewMallocSize ) );
		IVP_ASSERT( new_elems != nullptr );

		// dimhotepus: Elements are trivially copyable (static_assert in the
		// header), so one memcpy replaces the element-by-element loop.
		std::memcpy( new_elems, elems, sizeof( IVP_U_Min_List_Element ) * malloced_size );

		const unsigned int old_size = malloced_size;
		malloced_size = nNewMallocSize;

		P_FREE( elems );
		elems = new_elems;

		return_index = old_size;
		e = &elems[old_size];

		// dimhotepus: Only link new free slots if there are any. The old code
		// set free_list = old_size + 1 unconditionally, which is out of range when
		// the new array has exactly one new slot, and then clobbered
		// elems[malloced_size - 1].next.
		unsigned int i = old_size + 1;
		if ( i < malloced_size )
		{
			free_list = i;
			for ( ; i < malloced_size; i++ )
			{
				elems[i].next = i + 1;
			}
			elems[malloced_size - 1].next = IVP_U_MINLIST_UNUSED;
		}
		else
		{
			free_list = IVP_U_MINLIST_UNUSED;
		}
	}

	e->element = elem;
	e->value = value;

#ifdef IVP_U_MINLIST_USELONG
	e->long_next = IVP_U_MINLIST_LONG_UNUSED;
#endif

	// dimhotepus: Added "first_element == UNUSED". min_value starts at
	// IVP_U_MINLIST_MAXVALUE (1e10) but the assert above allows values up to
	// P_FLOAT_MAX (1e16). A bigger value added to an empty list skipped this fast
	// path and then used elems[UNUSED] in release builds.
	if ( first_element == IVP_U_MINLIST_UNUSED || value <= min_value )
	{
		// quick insert first element
		min_value = value;
		e->next = first_element;
		if ( first_element != IVP_U_MINLIST_UNUSED )
		{
			elems[first_element].prev = return_index;
		}
		first_element = return_index;
		IVP_ASSERT( ( first_element == IVP_U_MINLIST_UNUSED ) ||
		            ( first_element < malloced_size ) );

		e->prev = IVP_U_MINLIST_UNUSED;

#ifndef NDEBUG
//		check();
#endif

		return return_index;
	}

	IVP_ASSERT( first_element != IVP_U_MINLIST_UNUSED );
	// find place to insert element, we know at least one exists

	// dimhotepus: int -> unsigned int.
	unsigned int lastj = first_element;

#ifdef IVP_U_MINLIST_USELONG
	// lastj is the last element after we want to insert
	// dimhotepus: int -> unsigned int.
	unsigned int lo = first_long;
	unsigned int max_cmp_len = 3;
	while ( lo != IVP_U_MINLIST_UNUSED )
	{
		// do _long jumps if possible
		IVP_U_Min_List_Element *flong = &elems[lo];
		if ( flong->value >= value )
		{
			break;
		}
		max_cmp_len++;
		lastj = lo;
		lo = flong->long_next;
	}

	// dimhotepus: int -> unsigned int.
	unsigned int firstj_after = lastj;
	unsigned int count_cmp = 0;
#endif

	// dimhotepus: Replaced "goto end" with straight-line code. Same links are
	// written: e goes between lastj (last element with a smaller value) and j
	// (first element not smaller, or UNUSED at the end of the list).
	unsigned int j = elems[lastj].next;
	while ( j != IVP_U_MINLIST_UNUSED )
	{
		const IVP_U_Min_List_Element *f = &elems[j];

#ifdef IVP_U_MINLIST_USELONG
		count_cmp++;
#endif
		if ( value > f->value )
		{
			lastj = j;
			j = f->next;
			continue;
		}
		break;
	}

	e->next = j;
	e->prev = lastj;
	elems[lastj].next = return_index;
	if ( j != IVP_U_MINLIST_UNUSED )
	{
		elems[j].prev = return_index;
	}

#ifdef IVP_U_MINLIST_USELONG
	// insert long jmp
	if ( count_cmp > max_cmp_len )
	{
		// dimhotepus: int -> unsigned int.
		unsigned int new_long_pos = firstj_after;

		// search new position for longjump
		// dimhotepus: int -> unsigned int.
		for ( unsigned int k = 2; k < max_cmp_len; k++ )
		{
			new_long_pos = elems[new_long_pos].next;
		}

		// dimhotepus: int -> unsigned int.
		unsigned int            next_of_first = elems[firstj_after].long_next;
		IVP_U_Min_List_Element *nl = &elems[new_long_pos];
		IVP_ASSERT( nl->long_next == IVP_U_MINLIST_LONG_UNUSED );

		if ( next_of_first != IVP_U_MINLIST_LONG_UNUSED )
		{
			// real long as first element
			nl->long_next = next_of_first;
			nl->long_prev = firstj_after;
			if ( next_of_first != IVP_U_MINLIST_UNUSED )
			{
				elems[next_of_first].long_prev = new_long_pos;
			}
			elems[firstj_after].long_next = new_long_pos;
		}
		else
		{ // insert long as first long
			nl->long_next = first_long;
			nl->long_prev = IVP_U_MINLIST_UNUSED;
			if ( first_long != IVP_U_MINLIST_UNUSED )
			{
				elems[first_long].long_prev = new_long_pos;
			}
			first_long = new_long_pos;
		}
	}
#endif

#ifndef NDEBUG
//	check();
#endif

	return return_index;
}

void IVP_U_Min_List::remove_minlist_elem( IVP_U_MINLIST_INDEX index )
{
	IVP_ASSERT( index < malloced_size );
	IVP_ASSERT( counter > 0 );

	IVP_U_Min_List_Element *e = &elems[index];
	unsigned int            prev = e->prev;
	unsigned int            next = e->next;
	if ( prev != IVP_U_MINLIST_UNUSED )
	{
		// not the first element
		elems[prev].next = next;
		if ( next != IVP_U_MINLIST_UNUSED )
		{
			elems[next].prev = prev;
		}
	}
	else
	{
		first_element = next;
		IVP_ASSERT( ( first_element == IVP_U_MINLIST_UNUSED ) ||
		            ( first_element < malloced_size ) );
		if ( next != IVP_U_MINLIST_UNUSED )
		{
			elems[next].prev = prev;
			min_value = elems[next].value;
		}
		else
		{
			min_value = IVP_U_MINLIST_MAXVALUE;
		}
	}

#ifdef IVP_U_MINLIST_USELONG
	unsigned int long_next = e->long_next;
	if ( long_next != IVP_U_MINLIST_LONG_UNUSED )
	{
		// element is used as a long insert
		unsigned int long_prev = e->long_prev;
		if ( long_next != IVP_U_MINLIST_UNUSED )
		{
			elems[long_next].long_prev = long_prev;
		}

		if ( long_prev == IVP_U_MINLIST_UNUSED )
		{
			first_long = long_next;
		}
		else
		{
			elems[long_prev].long_next = long_next;
		}
	}
#endif

	counter -= 1;
	e->next = free_list;
	free_list = index;

#ifndef NDEBUG
//	check();
#endif
}

void IVP_U_Min_List::check()
{
	// dimhotepus: Was "last_val = 0", which made the sortedness assert fail for
	// negative values. Start at the lowest float instead.
	constexpr IVP_U_MINLIST_FIXED_POINT lowest =
	-std::numeric_limits< IVP_U_MINLIST_FIXED_POINT >::max();

	{
		IVP_U_MINLIST_FIXED_POINT last_val = lowest;
		// dimhotepus: Bound the walk, so a corrupted (cyclic) list asserts
		// instead of looping forever.
		unsigned int steps = 0;
		for ( unsigned int i = first_element; i != IVP_U_MINLIST_UNUSED;
		      i = elems[i].next )
		{
			IVP_ASSERT( i < malloced_size );
			IVP_ASSERT( ++steps <= malloced_size );

			IVP_U_Min_List_Element *e = &elems[i];
			IVP_ASSERT( last_val <= e->value );
			last_val = e->value;

			if ( i == first_element )
			{
				IVP_ASSERT( e->prev == IVP_U_MINLIST_UNUSED );
			}
			else
			{
				IVP_ASSERT( elems[e->prev].next == i );
			}

			if ( e->next != IVP_U_MINLIST_UNUSED )
			{
				IVP_ASSERT( elems[e->next].prev == i );
			}
		}
		IVP_USE( last_val );
		IVP_USE( steps );
	}

#ifdef IVP_U_MINLIST_USELONG
	{
		IVP_U_MINLIST_FIXED_POINT last_val = lowest;
		unsigned int              steps = 0;
		for ( unsigned int i = first_long; i != IVP_U_MINLIST_UNUSED;
		      i = elems[i].long_next )
		{
			IVP_ASSERT( i < malloced_size );
			IVP_ASSERT( ++steps <= malloced_size );

			IVP_U_Min_List_Element *e = &elems[i];
			IVP_ASSERT( last_val <= e->value );
			last_val = e->value;

			if ( i == first_long )
			{
				IVP_ASSERT( e->long_prev == IVP_U_MINLIST_UNUSED );
			}
			else
			{
				IVP_ASSERT( elems[e->long_prev].long_next == i );
			}

			if ( e->long_next != IVP_U_MINLIST_UNUSED )
			{
				IVP_ASSERT( elems[e->long_next].long_prev == i );
			}
		}
		IVP_USE( last_val );
		IVP_USE( steps );
	}
#endif
}