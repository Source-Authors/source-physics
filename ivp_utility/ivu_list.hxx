// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PUBLIC

#ifndef _P_LIST_INCLUDED
#define _P_LIST_INCLUDED

// dimhotepus: Explicit include for std::intptr_t, do not rely on the PCH.
#include <cstdint>

// dimhotepus: Debug-only O(n) misuse detection (double insert, remove of a
// foreign element, reinsert without reset, broken links). Enabled in debug
// builds, define IVP_PLIST_NO_DEBUG_CHECKS to opt out when debug builds get too
// slow on very long lists. Release builds never pay for it.
#if !defined( IVP_PLIST_NO_DEBUG_CHECKS ) && ( defined( _DEBUG ) || !defined( NDEBUG ) )
#define IVP_PLIST_DEBUG_CHECKS 1
#endif

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

	// dimhotepus: O(n) membership test, meant for asserts. Bounded by len so a
	// cyclic (corrupted) list ends instead of hanging, and stops at the removed
	// marker instead of dereferencing it.
	bool contains( const T *elem ) const
	{
		int steps = 0;
		for ( const T *e = first; e && steps <= len; e = e->next, ++steps )
		{
			if ( e == elem )
				return true;
			if ( e->next == removed_marker() )
				return false;
		}
		return false;
	}

	// dimhotepus: O(n) consistency check, meant for asserts. Verifies prev links,
	// that no linked element carries the removed marker, and that the number of
	// reachable elements equals len (also catches cycles).
	bool validate_links() const
	{
		int      count = 0;
		const T *prev = nullptr;
		for ( const T *e = first; e; e = e->next )
		{
			if ( e->prev != prev )
				return false;
			if ( ++count > len )
				return false;
			if ( e->next == removed_marker() )
				return false;
			prev = e;
		}
		return count == len;
	}

	void insert( T *elem )
	{
#ifdef IVP_PLIST_DEBUG_CHECKS
		// dimhotepus: Catch corruption from an earlier operation here, close to
		// the cause, not at some random later access.
		IVP_ASSERT( validate_links() );
		// dimhotepus: Separate from the !next check below so the failure points
		// at the real mistake: element was removed, reset next to nullptr first.
		IVP_ASSERT( elem->next != removed_marker() );
		// dimhotepus: Double insert makes the list cyclic and loses elements.
		IVP_ASSERT( !contains( elem ) );
#endif
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
#ifdef IVP_PLIST_DEBUG_CHECKS
		IVP_ASSERT( validate_links() );
		// dimhotepus: Removing a never inserted element (next == prev == nullptr)
		// looks like a one-element list and would reset first to nullptr, losing
		// the whole list. Same for an element that lives in another list.
		IVP_ASSERT( elem->next == removed_marker() || contains( elem ) );
#endif
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
#ifdef IVP_PLIST_DEBUG_CHECKS
		// dimhotepus: Poison the stale link in debug only (one extra store we do
		// not want on the release hot path). A later use of prev now fails
		// visibly with a null dereference instead of silently walking a live list.
		elem->prev = nullptr;
#endif
		len--;
		// elem->next = 0;
	}
};

// dimhotepus: Removed the commented-out P_Simple_List. It used 'register'
// (removed in C++17), referenced an undefined 'elem' in remove_first() and read
// 'prev' on a type documented as having only 'next'.

#endif
