#ifndef _IVP_ACTIVE_VALUE_HASH_INCLUDED
#define _IVP_ACTIVE_VALUE_HASH_INCLUDED

#include "ivu_vhash.hxx"

class IVP_U_Active_Value;

class IVP_Active_Value_Hash : protected IVP_VHash
{
protected:
	IVP_BOOL compare( const void *elem0, const void *elem1 ) const override;
	int      object_to_index( IVP_U_Active_Value *av );

public:
	explicit IVP_Active_Value_Hash( int init_size ) :
		IVP_VHash( init_size )
	{
	}
	~IVP_Active_Value_Hash();

	// dimhotepus: Owns references to its elements. A copy would drop them twice.
	IVP_Active_Value_Hash( const IVP_Active_Value_Hash & ) = delete;
	IVP_Active_Value_Hash &operator=( const IVP_Active_Value_Hash & ) = delete;

	// dimhotepus: Bodies moved to the .cxx. They call add_reference() /
	// remove_reference(), which needs the complete IVP_U_Active_Value, but this
	// header only forward-declares it, so it only compiled after
	// ivu_active_value.hxx happened to be included first.
	void add_active_value( IVP_U_Active_Value *av );

	// dimhotepus: void* -> bool. The old version returned a pointer that may
	// already have been deleted by remove_reference(), and crashed on null when
	// the element was not found. Returns true if an element was removed.
	bool remove_active_value( IVP_U_Active_Value *av );

	IVP_U_Active_Value *find_active_value( IVP_U_Active_Value *av );
};

#endif