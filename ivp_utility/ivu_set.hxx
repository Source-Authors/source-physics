// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PUBLIC

#ifndef IVP_SET_INCLUDED
#define IVP_SET_INCLUDED

#include "ivu_vhash.hxx"

/********************************************************************************
 *	Class:	       	IVP_U_Set.hxx
 *	Description:	Set of pointers
 ********************************************************************************/
// For threadsave usage
template< class T >
class IVP_U_Set_Enumerator
{
	int index;

public:
	// dimhotepus: explicit, single-argument constructor.
	explicit inline IVP_U_Set_Enumerator( IVP_VHash *vec ) { this->index = vec->len() - 1; }

	inline T *get_next_element( IVP_VHash *vec )
	{
		while ( true )
		{
			if ( this->index < 0 )
				return nullptr;
			T *res = (T *)vec->element_at( this->index-- );
			if ( res )
				return ( res );
		}
	}
};

template< class T >
class IVP_U_Set : public IVP_VHash
{
	// friend class IVP_U_Set_Enumerator;
protected:
	IVP_BOOL compare( const void *elem0, const void *elem1 ) const override
	{
		return ( elem0 == elem1 ) ? IVP_TRUE : IVP_FALSE;
	}

	// dimhotepus: static, it uses no members. This lets const methods (find_element)
	// call it.
	static int elem_to_index( T *elem )
	{
		return fast_hash_index( reinterpret_cast< hk_intp >( elem ) );
	}

public:
	void add_element( T *elem ) { IVP_VHash::add_elem( elem, elem_to_index( elem ) ); }

	// dimhotepus: void -> bool. Returns true if the element was in the set. Since
	// IVP_VHash::remove_elem now returns nullptr for a missing element (it used to
	// raise SIGINT), callers and IVP_U_Set_Active need to tell the two cases apart.
	bool remove_element( T *elem )
	{
		return IVP_VHash::remove_elem( elem, elem_to_index( elem ) ) != nullptr;
	}

	// returns nullptr or elem
	// dimhotepus: const, find_elem is const.
	T *find_element( T *elem ) const
	{
		return (T *)IVP_VHash::find_elem( elem, elem_to_index( elem ) );
	}

	void install_element( T *elem )
	{
		int index = elem_to_index( elem );
		T  *hit = (T *)IVP_VHash::find_elem( elem, index );
		if ( hit )
			return;

		IVP_VHash::add_elem( elem, index );
	}

	// dimhotepus: Removed "int n_elems()". It was non-const and hid the const
	// IVP_VHash::n_elems(), so it couldn't be called on a const set.

	~IVP_U_Set() override = default;
	// dimhotepus: explicit, single-argument constructor.
	explicit IVP_U_Set( int init_size ) :
		IVP_VHash( init_size )
	{
	}
};

template< class T >
class IVP_U_Set_Active;

template< class T >
class IVP_Listener_Set_Active
{
public:
	virtual void element_added( IVP_U_Set_Active< T > *set, T *elem ) = 0;
	virtual void element_removed( IVP_U_Set_Active< T > *set, T *elem ) = 0;
	virtual void pset_is_going_to_be_deleted(
	IVP_U_Set_Active< T >
	*set ) = 0; // Note: pset is not removing elements when deleted

protected:
	// dimhotepus: Interface with virtual functions but no virtual destructor.
	// Protected and non-virtual forbids deleting through the interface (UB).
	~IVP_Listener_Set_Active() = default;
};

// Note: add_element / install_element / remove_element below hide the base
// versions (they are not virtual). Calling them through an IVP_U_Set<T> pointer
// or reference skips the listener notifications.
template< class T >
class IVP_U_Set_Active : public IVP_U_Set< T >
{
protected:
	IVP_U_Vector< IVP_Listener_Set_Active< T > > listeners;

public:
	void add_element( T *elem )
	{
		IVP_U_Set< T >::add_element( elem );
		for ( int i = listeners.len() - 1; i >= 0; i-- )
		{
			IVP_Listener_Set_Active< T > *l = listeners.element_at( i );
			l->element_added( this, elem );
		}
	}

	void install_element( T *elem )
	{
		// dimhotepus: this->. elem_to_index lives in a dependent base, and
		// unqualified names are not looked up there. GCC and Clang reject the
		// unqualified call, MSVC (permissive) accepts it.
		int index = this->elem_to_index( elem );
		T  *hit = (T *)IVP_VHash::find_elem( elem, index );
		if ( hit )
			return;

		IVP_U_Set< T >::add_element( elem );
		for ( int i = listeners.len() - 1; i >= 0; i-- )
		{
			IVP_Listener_Set_Active< T > *l = listeners.element_at( i );
			l->element_added( this, elem );
		}
	}

	// dimhotepus: void -> bool, and only notify when an element was actually
	// removed. Notifying for a missing element sent listeners phantom
	// "element_removed" events. Previously that case never got this far because
	// remove_elem raised SIGINT.
	bool remove_element( T *elem )
	{
		if ( !IU_Set_Base::remove_element( elem ) )
			return false;
		for ( int i = listeners.len() - 1; i >= 0; i-- )
		{
			IVP_Listener_Set_Active< T > *l = listeners.element_at( i );
			l->element_removed( this, elem );
		}
		return true;
	}

	~IVP_U_Set_Active() override
	{
		for ( int i = listeners.len() - 1; i >= 0; i-- )
		{
			IVP_Listener_Set_Active< T > *l = listeners.element_at( i );
			l->pset_is_going_to_be_deleted( this );
		}
	}
	// dimhotepus: explicit, single-argument constructor.
	explicit IVP_U_Set_Active( int init_size ) :
		IVP_U_Set< T >( init_size )
	{
	}

	void add_listener_set_active( IVP_Listener_Set_Active< T > *lis )
	{
		listeners.add( lis );
	}

	void remove_listener_set_active( IVP_Listener_Set_Active< T > *lis )
	{
		listeners.remove( lis );
	}

private:
	using IU_Set_Base = IVP_U_Set< T >;
};

#endif
