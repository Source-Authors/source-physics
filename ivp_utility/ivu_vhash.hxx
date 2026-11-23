// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PUBLIC

#ifndef _IVP_VHASH_INCLUDED
#define _IVP_VHASH_INCLUDED

#include <cstddef>
#include <cstdint>
// dimhotepus: Declares IVP_Hash_crctab[256]. The header used an unsized extern.
#include <ivu_hash.hxx>

#define IVP_VHASH_TOUCH_BIT 0x80000000

/********************************************************************************
 *	File:        ivu_vhash.hxx
 *	Description: special, superfast hashclass base,
 *               key is calculated from the elems
 *               hash is automatically expanded but does not
 *               automatically shrink.
 *
 * Note:         highest bit of hash_index is used as a flag.
 ********************************************************************************/

struct IVP_VHash_Elem
{
	// unmasked index : highest bit is used to touch elements
	unsigned int hash_index;
	const void  *elem;
};

class IVP_VHash
{
private:
	void rehash( int new_size );

protected:
	int          size_mm; // size - 1, used for masking index
	unsigned int nelems : 24;
	// dimhotepus: IVP_BOOL -> unsigned int. Bit-fields of different types are not
	// packed together by MSVC, which made the object bigger than intended.
	unsigned int dont_free : 8; // indicating external memory

	IVP_VHash_Elem *elems;

	explicit IVP_VHash( int size );                      // assert(size = 2**x)
	IVP_VHash( IVP_VHash_Elem *static_elems, int size ); // assert(size = 2**x)

	// return TRUE if equal
	virtual IVP_BOOL compare( const void *elem0, const void *elem1 ) const = 0;

public:
	// dimhotepus: Owns (or borrows) the element array. A copy would double free.
	IVP_VHash( const IVP_VHash & ) = delete;
	IVP_VHash &operator=( const IVP_VHash & ) = delete;

	// useable index calculation, result is [0,0xfffffff]
	static inline int hash_index( const char *data, hk_intp size );
	// useable index calculation when size == 4 , result is [0,0xfffffff]
	static inline int fast_hash_index( hk_intp key );

	// touches element
	void add_elem( const void *elem, int hash_index );

	/* try to find and remove an element equals to elem, returns the element or
	 * null (dimhotepus: also when it is not in the hash). Elements left of elem
	 * are untouched.
	 */
	void *remove_elem( const void *elem, unsigned int hash_index );

	/* try to find an element equals to elem, *********/
	void *find_elem( const void *elem, unsigned int hash_index ) const;

	// Finds and touches.
	void *touch_element( const void *elem, unsigned int hash_index );

	void garbage_collection( int estimated_hash_size );

	void deactivate();                   // remove elems memory (assert no elems used)
	void activate( int preferred_size ); // allocate memory

	int      len() const { return size_mm + 1; }                // size of hash array
	int      n_elems() const { return static_cast< int >( nelems ); } // elems in hash
	void    *element_at( int i ) const { return const_cast< void * >( elems[i].elem ); }
	IVP_BOOL is_element_touched( int i ) const
	{
		return (IVP_BOOL)( elems[i].hash_index >= IVP_VHASH_TOUCH_BIT );
	}
	void untouch_all();
	void print() const;
	void check(); // check internal consistency
	virtual ~IVP_VHash();
};

// overflow of hash is handled by shifting elems to higher positions in hash,
// note: the order of entries ( comparing each (hash_index&size_mm) ) is
// preserved

// basic function for calculating the hash_index
inline int IVP_VHash::hash_index( const char *key, hk_intp key_size )
{
	// dimhotepus: Same CRC as before (key_size bytes, in order), on uint32_t and
	// with an index loop instead of a moving pointer.
	const unsigned char *data = reinterpret_cast< const unsigned char * >( key );
	std::uint32_t        index = 0xffffffffU; //-V112
	for ( hk_intp i = 0; i < key_size; i++ )
	{
		index = IVP_Hash_crctab[( index ^ data[i] ) & 0xffU] ^ ( index >> 8 );
	}
	return static_cast< int >( index | IVP_VHASH_TOUCH_BIT ); // set touch bit
}

// basic function for calculating the hash_index of key is a hk_intp
inline int IVP_VHash::fast_hash_index( hk_intp key )
{
	// dimhotepus: Unsigned arithmetic. The old signed key * 1001 / key * 75
	// overflowed (UB) for large 64-bit addresses. Same bits for non-negative keys.
	constexpr unsigned keyBits = sizeof( key ) * 4;
	const hk_uintp     k = static_cast< hk_uintp >( key );
	const hk_uintp     mixed = ( ( k * 1001u ) >> keyBits ) + k * 75u; //-V112
	return static_cast< int >( static_cast< std::uint32_t >( mixed ) | IVP_VHASH_TOUCH_BIT );
}

struct IVP_VHash_Store_Elem
{
	// unmasked index : highest bit is used to touch elements
	unsigned int hash_index;
	void        *key_elem;
	void        *elem;
};

// special, superfast hashclass base,
// it can store a pointer to an associated key-pointer
// hash index is calculated from the key_pointer
// hash is automatically expanded but not automatically shrunken

// highest bit of hash_index is used as a flag

class IVP_VHash_Store
{
private:
	void rehash( int new_size );

protected:
	int                   size;
	int                   size_mm; // size - 1, used for masking index
	int                   nelems;
	IVP_VHash_Store_Elem *elems_store;
	void                 *dont_free; // indicating external memory

	// return TRUE if equal
	static inline IVP_BOOL compare_store_hash( void *pointer0, void *pointer1 );
	// useable index calculation, result is [0,0xfffffff]
	static inline int hash_index_store( const char *data, int size );
	static inline int void_pointer_to_index( void *p );

public:
	// touches element
	void add_elem( void *key_elem, void *store_elem );
	void add_elem( void *key_elem, void *store_elem, int hash_index );

	void change_elem( void *key_elem, void *store_elem );

	// Try to find and remove an element equals to key_elem, returns the element
	// or null (dimhotepus: also when not found, it used to loop forever in
	// release builds). Elements left of elem are untouched.
	void *remove_elem( void *key_elem );
	void *remove_elem( void *key_elem, unsigned int hash_index );

	// Try to find an element equals to elem.
	void *find_elem( void *key_elem );
	void *find_elem( void *key_elem, unsigned int hash_index );

	// Finds and touches.
	void *touch_element( void *key_elem, unsigned int hash_index );

	int      len() const { return size; }
	int      n_elems() const { return nelems; }
	// dimhotepus: const, it doesn't modify the hash.
	void    *element_at( int i ) const { return elems_store[i].elem; }
	IVP_BOOL is_element_touched( int i ) const
	{
		return (IVP_BOOL)( elems_store[i].hash_index >= IVP_VHASH_TOUCH_BIT );
	}
	void untouch_all();
	void print() const;
	void check() const; // check internal consistency

	explicit IVP_VHash_Store( int size ); // assert(size = 2**x)
	IVP_VHash_Store( IVP_VHash_Store_Elem *static_elems,
	                 int                   size ); // assert(size = 2**x)
	~IVP_VHash_Store();

	// dimhotepus: Owns (or borrows) the element array. A copy would double free.
	IVP_VHash_Store( const IVP_VHash_Store & ) = delete;
	IVP_VHash_Store &operator=( const IVP_VHash_Store & ) = delete;
};

// overflow of hash is handled by shifting elems to higher positions in hash,
// note: the order of entries ( comparing each (hash_index&size_mm) ) is
// preserved

// basic function for calculating the hash_index
inline int IVP_VHash_Store::hash_index_store( const char *key, int key_size )
{
	const unsigned char *data = reinterpret_cast< const unsigned char * >( key );
	std::uint32_t        index = 0xffffffffU; //-V112
	for ( int i = 0; i < key_size; i++ )
	{
		index = IVP_Hash_crctab[( index ^ data[i] ) & 0xffU] ^ ( index >> 8 );
	}
	return static_cast< int >( index | IVP_VHASH_TOUCH_BIT ); // set touch bit
}

inline int IVP_VHash_Store::void_pointer_to_index( void *p )
{
	// Reading the pointer's bytes through char is always legal.
	return hash_index_store( reinterpret_cast< const char * >( &p ), sizeof( p ) );
}

inline IVP_BOOL IVP_VHash_Store::compare_store_hash( void *pointer0,
                                                     void *pointer1 )
{
	return (IVP_BOOL)( pointer0 == pointer1 );
}

// For threadsave usage
template< typename T >
class IVP_VHash_Enumerator
{
	int index;

public:
	explicit IVP_VHash_Enumerator( IVP_VHash *vec ) { index = vec->len() - 1; }

	T *get_next_element( IVP_VHash *vec )
	{
		while ( true )
		{
			if ( index < 0 )
				return nullptr;

			T *res = (T *)vec->element_at( index-- );

			if ( res )
				return res;
		}
	}
};

#endif