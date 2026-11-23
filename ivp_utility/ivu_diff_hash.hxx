// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PROTECTED

#ifndef IVP_DIFF_HASH_INCLUDED
#define IVP_DIFF_HASH_INCLUDED

#include <climits>
#include <cstddef>
#include <cstring>

/****************************************************************
 * Description:	    resorts input base vector according to the
 *		    following rules
 *		    if an input entry is found it is moved to
 *		    the start of the vector
 * Input		    CMP is a class which defines an inline
 *calc_hash_index and in inline CMP::are_equal( BASE, SEARCH) FVECTOR is a
 *IVU_FVector<BASE> BASE is vector element class SEARCH is the class used as a
 *key and is passed to CMP::xxx() Note              Hash Key is based on 2
 *SEARCH elements: search:   the element to search for reference: the reference
 *element
 ****************************************************************/

template< class CMP, class FVECTOR, class BASE, class SEARCH >
class IVP_Diff_Hash
{
	// dimhotepus: The table is indexed with "& buffersize_minus_one", which only
	// works when the size is a power of two.
	[[nodiscard]] static constexpr bool is_power_of_two( int value ) noexcept
	{
		return value > 0 && ( value & ( value - 1 ) ) == 0;
	}

public:
	short   *hash_to_vector_index;
	int      buffersize_minus_one;
	IVP_BOOL private_buffer;
	FVECTOR *base_vector;
	int      n_found_objects;

	IVP_Diff_Hash( FVECTOR *base, short *buffer, int buffer_size,
	               SEARCH *reference )
	{
		const int base_len = base->len();

		// dimhotepus: Table entries are short and -1 marks a free slot, so vector
		// indices must fit in 0..SHRT_MAX. Larger vectors silently truncated.
		IVP_ASSERT( base_len <= SHRT_MAX );

		// dimhotepus: Also use a private buffer when the caller's size is not a
		// power of two. The mask below made such a buffer probe wrong slots.
		if ( buffer_size <= base_len || !is_power_of_two( buffer_size ) )
		{
			private_buffer = IVP_TRUE;

			// dimhotepus: Round up to a power of two, starting from 1. The old
			// "x = buffer_size" looped forever for buffer_size <= 0 and kept a
			// non-power-of-two size. The cap keeps the shift from overflowing int.
			int x = 1;
			while ( x < buffer_size && x < ( 1 << 16 ) )
				x <<= 1;
			while ( x <= ( base_len << 1 ) )
				x <<= 1;
			buffersize_minus_one = x - 1;

			// dimhotepus: size_t math (x * sizeof( short ) was int), and check
			// the allocation.
			hash_to_vector_index = static_cast< short * >(
				p_malloc( static_cast< std::size_t >( x ) * sizeof( short ) ) );
			IVP_ASSERT( hash_to_vector_index != nullptr );
		}
		else
		{
			private_buffer = IVP_FALSE;
			buffersize_minus_one = buffer_size - 1;
			hash_to_vector_index = buffer;
		}
		n_found_objects = 0;
		base_vector = base;

		// dimhotepus: Was a loop writing 0xffffffff through an (int *) cast of the
		// short buffer: a strict-aliasing violation, an alignment assumption on the
		// caller's buffer, and a 4-byte write into a 2-byte buffer when
		// buffer_size == 1. memset 0xff sets every short to -1 (the free marker).
		std::memset( hash_to_vector_index, 0xff,
		             static_cast< std::size_t >( buffersize_minus_one + 1 ) * sizeof( short ) );

		{
			// insert all existing elements
			// dimhotepus: A free slot always exists: the table is larger than the
			// vector (private: > 2x, caller buffer: buffer_size > len), so this
			// probe terminates.
			for ( int i = base_vector->len() - 1; i >= 0; i-- )
			{
				BASE *b = base_vector->element_at( i );
				for ( hk_intp index = CMP::calc_hash_index( b, reference );; index++ )
				{
					index &= buffersize_minus_one;
					if ( hash_to_vector_index[index] == -1 )
					{ // free place found
						hash_to_vector_index[index] = static_cast< short >( i );
						break;
					}
				}
			}
		}
	}

	// dimhotepus: Owns a malloc'ed buffer when private_buffer is set. A copy
	// would double free it.
	IVP_Diff_Hash( const IVP_Diff_Hash & ) = delete;
	IVP_Diff_Hash &operator=( const IVP_Diff_Hash & ) = delete;

	// returns true if element is already in base vector and resorts basevector
	// accordingly if found
	// reference is the reference element
	BASE *check_element( SEARCH *elem, SEARCH *reference )
	{
		for ( hk_intp index = CMP::calc_hash_index( elem );; index++ )
		{
			index &= buffersize_minus_one;
			int vector_index = hash_to_vector_index[index];
			if ( vector_index == -1 )
			{
				return nullptr;
			}
			BASE *base_elem = base_vector->element_at( vector_index );
			if ( !CMP::are_equal( base_elem, elem ) )
			{
				continue;
			}
			// now we found the element
			IVP_ASSERT( vector_index >= n_found_objects );

			// dimhotepus: The assert above vanishes in release builds. An element
			// found a second time already sits in the front part, so incrementing
			// n_found_objects again broke the "found elements come first"
			// invariant. Return it without counting it twice.
			if ( vector_index < n_found_objects )
			{
				return base_elem;
			}

			if ( vector_index > n_found_objects )
			{
				// swap elems in base vector:  n_found_objects : vector_index
				BASE *nfb = base_vector->element_at( n_found_objects );
				// search and mark existing position
				for ( hk_intp i = CMP::calc_hash_index( nfb, reference );; i++ )
				{
					i &= buffersize_minus_one;
					if ( hash_to_vector_index[i] != n_found_objects )
					{
						continue;
					}
					// dimhotepus: Entries are short, see the constructor.
					hash_to_vector_index[i] = static_cast< short >( vector_index );
					hash_to_vector_index[index] = static_cast< short >( n_found_objects );
					base_vector->swap_elems( n_found_objects, vector_index );
					break;
				}
			}
			n_found_objects++;
			return base_elem;
		}
	}

	~IVP_Diff_Hash()
	{
		if ( private_buffer )
		{
			P_FREE( hash_to_vector_index );
		}
	}
};

#endif