// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#ifndef IVP_U_MINLIST_INCLUDED
#define IVP_U_MINLIST_INCLUDED
// IVP_EXPORT_PUBLIC

#include <type_traits>  // std::is_trivially_copyable

using IVP_U_MINLIST_FIXED_POINT = float;
constexpr inline float IVP_U_MINLIST_MAXVALUE{ 1e10f };

#define IVP_U_MINLIST_USELONG
using IVP_U_MINLIST_INDEX = unsigned int;

// dimhotepus: Bump 65535 -> 262143.
// dimhotepus: int -> unsigned int. These are compared with and assigned to
// unsigned indices everywhere, so signed constants only caused sign-compare
// warnings and implicit conversions.
constexpr inline unsigned int IVP_U_MINLIST_UNUSED{ ( 1u << 18 ) - 1u };
// dimhotepus: Bump 65534 -> 262142.
constexpr inline unsigned int IVP_U_MINLIST_LONG_UNUSED{ ( 1u << 18 ) - 2u };
// dimhotepus: Bump 65532 -> 262140.
constexpr inline unsigned int IVP_U_MINLIST_MAX_ALLOCATION{ ( 1u << 18 ) - 4u };

// dimhotepus: Every valid index must be distinct from both sentinels.
static_assert( IVP_U_MINLIST_MAX_ALLOCATION < IVP_U_MINLIST_LONG_UNUSED &&
               IVP_U_MINLIST_LONG_UNUSED < IVP_U_MINLIST_UNUSED,
               "min list sentinels must not collide with valid indices" );

class IVP_U_Min_List_Element
{
public:
	// dimhotepus: unsigned short -> unsigned int.
#ifdef IVP_U_MINLIST_USELONG
	unsigned int long_next;
	unsigned int long_prev;
#endif
	unsigned int              next;
	unsigned int              prev;
	IVP_U_MINLIST_FIXED_POINT value;
	void                     *element;
};

// dimhotepus: The list grows with memcpy, so elements must stay trivially
// copyable. Fail the build if someone adds a non-trivial member.
static_assert( std::is_trivially_copyable< IVP_U_Min_List_Element >::value,
               "IVP_U_Min_List_Element must be trivially copyable" );

class IVP_U_Min_List
{
	friend class IVP_U_Min_List_Enumerator;
	// dimhotepus: Reordered members to reduce size on x86-64.
	IVP_U_Min_List_Element *elems;
	// dimhotepus: unsigned short -> unsigned int.
	unsigned int malloced_size;
	unsigned int free_list;

public:
	IVP_U_MINLIST_FIXED_POINT min_value;
	// dimhotepus: unsigned short -> unsigned int.
#ifdef IVP_U_MINLIST_USELONG
	unsigned int first_long;
#endif
	unsigned int first_element;
	unsigned int counter;

	IVP_U_Min_List( int size = 4 );
	~IVP_U_Min_List();

	// dimhotepus: Class owns raw memory. A copy would double free in the dtor.
	IVP_U_Min_List( const IVP_U_Min_List & ) = delete;
	IVP_U_Min_List &operator=( const IVP_U_Min_List & ) = delete;

	IVP_U_MINLIST_INDEX add( void                     *elem,
	                         IVP_U_MINLIST_FIXED_POINT value ); // returns an index

	void *find_min_elem()
	{
		IVP_ASSERT( first_element != IVP_U_MINLIST_UNUSED );
		// dimhotepus: In release builds the assert vanishes and this indexed
		// elems[UNUSED], far outside the array, on an empty list.
		if ( first_element == IVP_U_MINLIST_UNUSED )
			return nullptr;
		return elems[first_element].element;
	}

	// dimhotepus: const, don't modify the list.
	IVP_BOOL has_elements() const { return (IVP_BOOL)( counter > 0 ); }

	IVP_U_MINLIST_FIXED_POINT find_min_value() const { return min_value; }

	void prefetch0_minlist()
	{
		IVP_IF_PREFETCH_ENABLED( IVP_TRUE )
		{
			IVP_PREFETCH( this, 0 );
		}
	}

	void prefetch1_minlist()
	{
		IVP_IF_PREFETCH_ENABLED( IVP_TRUE )
		{
			// dimhotepus: &elems[UNUSED] is pointer arithmetic far outside the
			// allocation (UB) for an empty list / no long jumps. Guard it.
			if ( first_element != IVP_U_MINLIST_UNUSED )
			{
				IVP_PREFETCH_BLOCK( &elems[first_element], 64 ); // get at least 64 bytes
			}
#ifdef IVP_U_MINLIST_USELONG
			if ( first_long != IVP_U_MINLIST_UNUSED )
			{
				IVP_PREFETCH( &elems[first_long], 0 );
			}
#endif
		}
	}

	void remove_minlist_elem( IVP_U_MINLIST_INDEX index );
	void check();
};

class IVP_U_Min_List_Enumerator
{
	IVP_U_Min_List     *min_list;
	IVP_U_MINLIST_INDEX loop_elem;

public:
	IVP_U_Min_List_Enumerator( IVP_U_Min_List *mh )
	{
		min_list = mh;
		loop_elem = mh->first_element;
	}

	void *get_next_element()
	{
		void *e;
		if ( loop_elem != IVP_U_MINLIST_UNUSED )
		{
			e = min_list->elems[loop_elem].element;
			loop_elem = min_list->elems[loop_elem].next;
		}
		else
		{
			e = nullptr;
		}
		return e;
	}

	IVP_U_Min_List_Element *get_next_element_header()
	{
		IVP_U_Min_List_Element *el;
		if ( loop_elem != IVP_U_MINLIST_UNUSED )
		{
			el = &min_list->elems[loop_elem];
			loop_elem = el->next;
		}
		else
		{
			el = nullptr;
		}
		return el;
	}

	void *get_next_element_lt( IVP_FLOAT max_limit )
	{
		void *e;
		if ( loop_elem != IVP_U_MINLIST_UNUSED )
		{
			IVP_U_Min_List_Element *el = &min_list->elems[loop_elem];
			if ( el->value >= max_limit )
				return nullptr;
			loop_elem = el->next;
			e = el->element;
		}
		else
		{
			e = nullptr;
		}
		return e;
	}
};
#endif
