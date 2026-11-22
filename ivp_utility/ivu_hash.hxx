#ifndef IVP_HASH_INCLUDED
#define IVP_HASH_INCLUDED

class IVP_Hash_Elem;

// dimhotepus: Declare the exact size so the compiler checks the definition
// against it. The old unsized declaration hid a corrupted table.
extern const unsigned int IVP_Hash_crctab[256];

class IVP_Hash
{
	friend class IVP_U_String_Hash;
	int             key_size;
	int             size;
	void           *not_found_value;
	IVP_Hash_Elem **elems;

public:
	// dimhotepus: Was declared inline but defined only in the .cxx. Other TUs
	// calling it got undefined symbols (or an ODR mismatch on MSVC).
	int hash_index( const char *key ) const;

	IVP_Hash( int size, int key_size, void *not_found_value = nullptr );
	~IVP_Hash();

	// dimhotepus: Class owns raw memory. A copy would double free in the dtor.
	IVP_Hash( const IVP_Hash & ) = delete;
	IVP_Hash &operator=( const IVP_Hash & ) = delete;

	void  add( const char *key, void *value );
	void  remove( const char *key );
	void *find( const char *key ) const;
};

/** class for looping through the Hash,
  if you want to delete current element, call next_element first,
  Note: not implemented */
class IVP_Hash_Enumerator
{
	int            index_pos;
	IVP_Hash      *my_hash;
	IVP_Hash_Elem *el;

public:
	IVP_Hash_Enumerator( IVP_Hash *hash )
	{
		my_hash = hash;
		index_pos = -1;
		el = nullptr;
	}
	void *next_element();
};

/** typical usage:
 *	IVP_Hash_Enumerator en(hash);
 *	while  ( x = en.next_element()){
 *	}
 */

#endif