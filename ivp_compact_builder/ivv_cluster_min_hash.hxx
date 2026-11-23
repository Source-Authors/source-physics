// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// dimhotepus: Header had no include guard, so a double include was a
// redefinition error.
#ifndef _IVV_CLUSTER_MIN_HASH_INCLUDED
#define _IVV_CLUSTER_MIN_HASH_INCLUDED

#include <cstddef>

// dimhotepus: Guarded so it can't clash with the same define in ivu_min_hash.hxx.
#ifndef SORT_MINDIST_ELEMENTS
#define SORT_MINDIST_ELEMENTS
#endif

// dimhotepus: Note: on 64-bit targets `spheres` covers only 4 of the 8 bytes of
// `key`, and reading `key` after writing `spheres` is union type punning. It is
// not ISO C++, though GCC, Clang and MSVC all define it. Zero `key` first when
// building a key from `spheres`. Unchanged here, as it is not used in this file.
union IVV_Cluster_Min_Hash_Key
{
	struct
	{
		unsigned int s1 : 16;
		unsigned int s2 : 16;
	} spheres;
	hk_uintp key;
};

class IVV_Cluster_Min_Hash_Elem
{
public:
	// dimhotepus: Default member initializers, so a default-constructed element
	// is always in a defined state.
	IVV_Cluster_Min_Hash_Elem *next = nullptr;
	IVP_DOUBLE                 value = 0;
#if defined( SORT_MINDIST_ELEMENTS )
	// dimhotepus: int -> unsigned. A signed counter overflowing is UB.
	unsigned int cmp_index = 0;
#endif
	void *elem = nullptr;

	IVV_Cluster_Min_Hash_Elem() = default;
	~IVV_Cluster_Min_Hash_Elem(); // deletes the whole chain starting at next

	// dimhotepus: The destructor deletes the chain, so a copy would double free.
	IVV_Cluster_Min_Hash_Elem( const IVV_Cluster_Min_Hash_Elem & ) = delete;
	IVV_Cluster_Min_Hash_Elem &operator=( const IVV_Cluster_Min_Hash_Elem & ) = delete;
};

class IVV_Cluster_Min_Hash
{
	friend class IVV_Cluster_Min_Hash_Enumerator;

	// dimhotepus: Takes the pointer value. Defined in the .cxx (it was a private
	// inline defined there, which is fragile). Removed the dead, never defined
	// `static unsigned int crctab[]`. The .cxx uses IVP_Hash_crctab.
	int hash_index( const void *key ) const;

	// dimhotepus: Reordered members to reduce size on x86-64.
	IVV_Cluster_Min_Hash_Elem **stadel;
	IVV_Cluster_Min_Hash_Elem **min_per_array_pos;
	IVV_Cluster_Min_Hash_Elem **elems;
	unsigned int                size;
#if defined( SORT_MINDIST_ELEMENTS )
	// dimhotepus: Per-instance tie-break counter. It replaces a function-local
	// static in add() that was shared by all instances and not thread-safe.
	unsigned int next_cmp_index;
#endif

	// dimhotepus: int -> hk_intp for index math, consistent with IVP_U_Min_Hash.
	void min_added_at_index( IVV_Cluster_Min_Hash_Elem *elem, hk_intp i );
	void min_removed_at_index( IVV_Cluster_Min_Hash_Elem *elem, hk_intp i );

public:
	int counter;

	IVV_Cluster_Min_Hash( int size = 256 ); // size must be 2^x
	~IVV_Cluster_Min_Hash();

	// dimhotepus: Class owns raw memory. A copy would double free in the dtor.
	IVV_Cluster_Min_Hash( const IVV_Cluster_Min_Hash & ) = delete;
	IVV_Cluster_Min_Hash &operator=( const IVV_Cluster_Min_Hash & ) = delete;

	void add( void *elem, IVP_DOUBLE value );
	void change_value( void *elem, IVP_DOUBLE new_value );

	void *find_min_elem()
	{
		if ( !stadel[1] )
			return nullptr;
		return stadel[1]->elem;
	}
	// dimhotepus: Was an unchecked null dereference on an empty hash.
	// Precondition: hash is not empty (find_min_elem() != nullptr).
	IVP_DOUBLE find_min_value() const
	{
		IVP_ASSERT( stadel[1] != nullptr );
		return stadel[1]->value;
	}
	// dimhotepus: const, since it doesn't modify the hash. Return type kept as
	// int for source compatibility.
	int  is_elem( void *elem ) const;
	void remove( void *elem );
	void remove_min();
};

class IVV_Cluster_Min_Hash_Enumerator
{
	IVV_Cluster_Min_Hash      *min_hash;
	IVV_Cluster_Min_Hash_Elem *loop_elem;
	int                        loop_index;

public:
	IVV_Cluster_Min_Hash_Enumerator( IVV_Cluster_Min_Hash *mh )
	{
		min_hash = mh;
		loop_elem = nullptr;
		loop_index = -1;
	}

	void *get_next_element()
	{
		if ( loop_elem )
		{
			loop_elem = loop_elem->next;
		}
		while ( !loop_elem )
		{
			loop_index++;
			if ( loop_index >= static_cast< int >( min_hash->size ) )
				return nullptr;
			loop_elem = min_hash->elems[loop_index];
		}
		return loop_elem->elem;
	}

	void *get_next_element_lt( IVP_DOUBLE max_limit )
	{
		while ( true )
		{
			if ( loop_elem )
			{
				loop_elem = loop_elem->next;
			}
			while ( !loop_elem )
			{
				loop_index++;
				if ( loop_index >= static_cast< int >( min_hash->size ) )
					return nullptr;
				loop_elem = min_hash->elems[loop_index];
			}
			if ( loop_elem->value < max_limit )
			{
				return loop_elem->elem;
			}
		}
	}
};

#endif