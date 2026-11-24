// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PUBLIC

#ifndef _P_LIST_INCLUDED
#define _P_LIST_INCLUDED

#include <cstdint>  // std::intptr_t

/* list header, list elements should have:
   next, prev
   */

template< class T >
class P_List
{
	// dimhotepus: One named place for the "removed" marker instead of two
	// C-style (T *)-1 casts. Same all-ones value on x86, x86-64 and arm64.
	static T *removed_marker()
	{
		return reinterpret_cast< T * >( static_cast< std::intptr_t >( -1 ) );
	}

public:
	// dimhotepus: Default member initializers, no constructor path can leave
	// them uninitialized.
	T  *first{ nullptr };
	int len{ 0 };

	P_List() = default;

	// dimhotepus: Two headers sharing the same first element corrupt each other
	// on remove(), so copying a list header is never correct.
	P_List( const P_List & ) = delete;
	P_List &operator=( const P_List & ) = delete;

	void insert( T *elem )
	{
		IVP_ASSERT( !elem->next );
		elem->next = this->first;
		if ( first )
		{
			first->prev = elem;
		}
		// dimhotepus: nullptr instead of 0.
		elem->prev = nullptr;
		first = elem;
		len++;
	}

	void remove( T *elem )
	{
		IVP_ASSERT( elem->next != removed_marker() );
		IVP_ASSERT( len > 0 );

		// dimhotepus: IVP_ASSERT is compiled out in release. Removing an element
		// twice followed the marker as a real pointer and corrupted the list, and
		// drove len negative.
		if ( elem->next == removed_marker() )
			return;

		// dimhotepus: Read prev / next once. The old code reloaded them after
		// stores through other T pointers, which the compiler must assume alias.
		T *const prev = elem->prev;
		T *const next = elem->next;

		if ( prev )
		{
			prev->next = next;
		}
		else
		{
			first = next;
		}
		if ( next )
		{
			next->prev = prev;
		}

		elem->next = removed_marker(); // invalid flag !!
		len--;
		// elem->next = 0;
	}
};

// dimhotepus: Removed the commented-out P_Simple_List. It used 'register'
// (removed in C++17), referenced an undefined 'elem' in remove_first() and read
// 'prev' on a type documented as having only 'next'.

#endif
