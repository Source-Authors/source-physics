// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PUBLIC

#ifndef _IVP_U_MINHASH_INCLUDED
#define _IVP_U_MINHASH_INCLUDED

// dimhotepus: Header uses std::uintptr_t, so include what it uses.
#include <cstddef>
#include <cstdint>

// #ifdef DEBUG
#define SORT_MINDIST_ELEMENTS
// #endif

struct IVP_U_Min_Hash_Elem
{
	IVP_U_Min_Hash_Elem *next;
	IVP_DOUBLE           value;
#if defined( SORT_MINDIST_ELEMENTS )
	// dimhotepus: int -> unsigned. A signed counter overflowing is UB.
	unsigned int cmp_index;
#endif
	void *elem;
};

// dimhotepus: Removed unused extern IVP_Hash_crctab. Nothing in this header
// uses it, and it duplicated a declaration from ivu_hash.hxx.

class IVP_U_Min_Hash
{
	friend class IVP_U_Min_Hash_Enumerator;

	// dimhotepus: Takes the pointer value instead of a pointer to it. The old
	// (const hk_intp *)&elem read a void* through an hk_intp lvalue, which is a
	// strict-aliasing violation (miscompiles under GCC/Clang -O2).
	// dimhotepus: Arithmetic is unsigned. The old signed multiply overflowed on
	// large 64-bit addresses (UB). Bucket values are unchanged.
	hk_intp hash_index( const void *key ) const
	{
		const std::uintptr_t k = reinterpret_cast< std::uintptr_t >( key );
		const std::uintptr_t x = k * 101u;
		const std::uintptr_t y = ( x >> 8 ) + k * 1001u;
		return static_cast< hk_intp >( y & ( static_cast< std::uintptr_t >( size ) - 1u ) );
	}

	// dimhotepus: Reordered members to reduce size on x86-64.
	IVP_U_Min_Hash_Elem **stadel;
	IVP_U_Min_Hash_Elem **min_per_array_pos;
	IVP_U_Min_Hash_Elem **elems;
	unsigned int          size;
#if defined( SORT_MINDIST_ELEMENTS )
	// dimhotepus: Per-instance tie-break counter. It replaces a function-local
	// static in add() that was shared by all instances and not thread-safe.
	unsigned int next_cmp_index;
#endif

	void min_added_at_index( IVP_U_Min_Hash_Elem *elem, hk_intp i );
	void min_removed_at_index( IVP_U_Min_Hash_Elem *elem, hk_intp i );

public:
	int counter;

	IVP_U_Min_Hash( int size = 256 ); // size must be 2^x
	~IVP_U_Min_Hash();

	// dimhotepus: Class owns raw memory. A copy would double free in the dtor.
	IVP_U_Min_Hash( const IVP_U_Min_Hash & ) = delete;
	IVP_U_Min_Hash &operator=( const IVP_U_Min_Hash & ) = delete;

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
	bool is_elem( void *elem ) const;
	void remove( void *elem );
	void remove_min();
};

class IVP_U_Min_Hash_Enumerator
{
	IVP_U_Min_Hash      *min_hash;
	IVP_U_Min_Hash_Elem *loop_elem;
	int                  loop_index;

public:
	IVP_U_Min_Hash_Enumerator( IVP_U_Min_Hash *mh )
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
