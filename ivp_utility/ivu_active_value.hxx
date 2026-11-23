// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_EXPORT_PUBLIC

// IVP_U_Active_Float ist ein modular kombinierbarer Zahlengeber (Synthesizer)
// und Event-Ausloeser zur Implementierung der Spiel-Logik ausserhalb
// der eigentlichen Grundphysik.
// z.B. fuer IVP_Actuator_Forces (oder Klaenge :-)

#ifndef _IVP_ACTIVE_VALUES_INCLUDED
#define _IVP_ACTIVE_VALUES_INCLUDED

#include <atomic>

class IVP_U_Active_Value;
class IVP_U_String_Hash;
class IVP_U_Active_Float;
class IVP_U_Active_Int;
class IVP_U_Active_Float_Delayed;
class IVP_U_Active_Int_Delayed;
class IVP_U_Active_Terminal_Double;
class IVP_U_Active_Terminal_Int;
class IVP_Active_Value_Hash;
class IVP_Environment;

#define IVP_U_MOD_NAME_HASH_SIZE 16

#define IVP_ACTIVE_FLOAT_CURRENT_TIME_NAME "current_time"

/********************************************************************************
 *	Name:	       	IVP_U_Active_Value_Manager
 *	Description:	a simple name to active_value hash
 *	Attention:	will not be freed when env is deleted (to do so use
 *IVP_U_Active_Value_Manager_EnvironmentLocal)
 ********************************************************************************/
class IVP_U_Active_Value_Manager
{
private:
	IVP_BOOL delete_on_env_delete; // see deconstructor

	IVP_Active_Value_Hash                     *floats_name_hash;
	IVP_Active_Value_Hash                     *ints_name_hash;
	IVP_U_Vector< IVP_U_Active_Float_Delayed > delayed_active_floats;
	IVP_U_Vector< IVP_U_Active_Int_Delayed >   delayed_active_ints;

	IVP_U_Active_Terminal_Double *mod_current_time;
	IVP_U_Active_Value           *search_active_value;

	// dimhotepus: One place for the "temporarily rename the probe value and
	// look it up" trick that was copy-pasted in six functions. Returns nullptr
	// for a null name (the hash would call strlen on it).
	IVP_U_Active_Value *find_named( IVP_Active_Value_Hash *hash, const char *name );

public:
	IVP_U_Active_Value_Manager( IVP_BOOL delete_on_env_delete );
	virtual ~IVP_U_Active_Value_Manager();

	// dimhotepus: Owns the hashes and the probe value. A copy would double free.
	IVP_U_Active_Value_Manager( const IVP_U_Active_Value_Manager & ) = delete;
	IVP_U_Active_Value_Manager &operator=( const IVP_U_Active_Value_Manager & ) = delete;

	virtual void environment_will_be_deleted( IVP_Environment * )
	{
		if ( delete_on_env_delete )
		{
			P_DELETE_THIS( this );
		}
	}

	// dimhotepus: void -> bool.
	virtual bool insert_active_float(
	IVP_U_Active_Float *mod ); // add to name_hash
	virtual void remove_active_float(
	IVP_U_Active_Float *mod ); // remove from name_hash

	// dimhotepus: void -> bool, same as insert_active_float, so callers can
	// tell when the insert was rejected and free the value.
	virtual bool insert_active_int( IVP_U_Active_Int *mod ); // add to name_hash
	virtual void remove_active_int(
	IVP_U_Active_Int *mod ); // remove from name_hash

	virtual void delay_active_float(
	IVP_U_Active_Float_Delayed
	*mod ); // put into delay_active_IVP_FLOAT queue, so update can be
	        // executed later (next PSI)
	virtual void delay_active_int(
	IVP_U_Active_Int_Delayed
	*mod ); // put into delay_active_IVP_FLOAT queue, so update can be
	        // executed later (next PSI)

	virtual void update_delayed_active_values();

	virtual void init_active_values_generic(); // current_time, double_null, ...

	virtual void refresh_psi_active_values( IVP_Environment *env );

	/********************************************************************************
	 *	Name:	       	install_active_float
	 *	Description:	searches for active float, returns active IVP_FLOAT if
	 *already present, otherwise creates it
	 ********************************************************************************/
	virtual IVP_U_Active_Float           *install_active_float( const char *i_name,
	                                                            IVP_DOUBLE  value );
	virtual IVP_U_Active_Terminal_Double *create_active_float( const char *i_name,
	                                                           IVP_DOUBLE  value );
	IVP_U_Active_Float                   *get_active_float_by_name( const char *i_name );

	virtual IVP_U_Active_Int          *install_active_int( const char *i_name, int value );
	virtual IVP_U_Active_Terminal_Int *create_active_int( const char *i_name,
	                                                      int         value );
	IVP_U_Active_Int                  *get_active_int_by_name( const char *i_name );
};

///////////////

class IVP_U_Active_Float_Listener
{
	// Classes that want to be called at a active_IVP_FLOAT update
	// must be derived from IVP_U_Active_Float_Listener and must
	// implement the update method.

	// If more than one event could be possible, you can find out
	// which active_IVP_FLOAT triggered the current event by comparing with the
	// 'calling_active_float' parameter.

public:
	virtual void active_float_changed(
	IVP_U_Active_Float *calling_active_IVP_FLOAT ) = 0;

protected:
	// dimhotepus: Interface with virtual functions but no virtual destructor.
	// Protected and non-virtual forbids deleting through this interface, which
	// would be UB.
	~IVP_U_Active_Float_Listener() = default;
};

///////////////

class IVP_U_Active_Int_Listener
{
	// Classes that want to be called at a active_int update
	// must be derived from IVP_U_Active_Float_Listener and must
	// implement the update method.

	// If more than one event could be possible, you can find out
	// which active_IVP_FLOAT triggered the current event by comparing with the
	// 'calling_active_float' parameter.

public:
	virtual void active_int_changed( IVP_U_Active_Int *calling_active_int ) = 0;

protected:
	// dimhotepus: See IVP_U_Active_Float_Listener.
	~IVP_U_Active_Int_Listener() = default;
};

class IVP_U_Active_Int_Delayed
{
public:
	virtual void update_int() = 0;

protected:
	// dimhotepus: See IVP_U_Active_Float_Listener.
	~IVP_U_Active_Int_Delayed() = default;
};

class IVP_U_Active_Float_Delayed
{
public:
	virtual void update_float() = 0;

protected:
	// dimhotepus: See IVP_U_Active_Float_Listener.
	~IVP_U_Active_Float_Delayed() = default;
};

///////////////
class IVP_U_Active_Value
{
private:
	friend class IVP_U_Active_Value_Manager;
	// dimhotepus: char* -> const char*
	const char *name;

protected:
	int reference_count;

public:
	void add_reference() { reference_count++; }
	void remove_reference()
	{
		// dimhotepus: Was "reference_count--; if ( !reference_count )". An extra
		// remove drove the count negative, so the object was never freed. Assert
		// and test the decremented value.
		IVP_ASSERT( reference_count > 0 );
		if ( --reference_count == 0 )
			delete this;
	}
	virtual ~IVP_U_Active_Value();
	// dimhotepus: explicit, and a null name is allowed (the manager's probe
	// value has none).
	explicit IVP_U_Active_Value( const char *name_ );

	// dimhotepus: Reference counted and owns its name. A copy would double free.
	IVP_U_Active_Value( const IVP_U_Active_Value & ) = delete;
	IVP_U_Active_Value &operator=( const IVP_U_Active_Value & ) = delete;

	const char *get_name() const { return name; }
};

class IVP_U_Active_Float : public IVP_U_Active_Value
{
	friend class IVP_U_Active_Value_Manager;

private:
	// compiled dependant active_floats
	IVP_U_Vector< IVP_U_Active_Float_Listener > derived_mods;

	// dep could be removed when update_derived() doesn't find the entry in
	// name_hash
protected:
	IVP_U_Active_Value_Manager *l_mod_manager; // backlink
public:
	// dimhotepus: int -> unsigned int. change_meter is incremented on every set,
	// and signed overflow is UB. Unsigned wraps, with the same equality
	// semantics as before.
	unsigned int last_update;  // contains value of change_meter at last update
	IVP_DOUBLE   double_value; // for IVP_U_MOD_TYPE_DOUBLE active_floats
	void         update_derived();

public:
	explicit IVP_U_Active_Float( const char *name );
	virtual ~IVP_U_Active_Float();
public:
	// dimhotepus: std::atomic. The meter is shared by every manager and
	// incremented on every set_*(). With environments on different threads a
	// plain unsigned int was a data race (UB). Still public and still usable as
	// `change_meter++` / `== change_meter` from other files (atomic operators),
	// but prefer the two helpers below.
	static std::atomic< unsigned int > change_meter;

	// dimhotepus: Acquire pairs with the release in bump_change_meter(): a thread
	// that sees the new stamp also sees the value written before the bump.
	static unsigned int load_change_meter() noexcept
	{
		return change_meter.load( std::memory_order_acquire );
	}
	static void bump_change_meter() noexcept
	{
		change_meter.fetch_add( 1u, std::memory_order_release );
	}

	IVP_DOUBLE give_double_value() const { return double_value; }
	IVP_FLOAT  get_float_value() const { return static_cast< IVP_FLOAT >( double_value ); }

	void add_dependency( IVP_U_Active_Float_Listener *derived_active_IVP_FLOAT );
	void remove_dependency( IVP_U_Active_Float_Listener *derived_active_IVP_FLOAT );

	virtual int print() = 0;
};

///////////////

class IVP_U_Active_Int : public IVP_U_Active_Value
{
	friend class IVP_U_Active_Value_Manager;

private:
	// compiled dependant active_ints
	IVP_U_Vector< IVP_U_Active_Int_Listener > derived_mods;

	// dep could be removed when update_derived() doesn't find the entry in
	// name_hash
protected:
	IVP_U_Active_Value_Manager *l_mod_manager; // backlink
public:
	// dimhotepus: int -> unsigned int, see IVP_U_Active_Float::last_update.
	unsigned int last_update; // contains value of change_meter at last update
	int          int_value;   // for IVP_U_MOD_TYPE_INT active_ints

	void update_derived();

public:
	int give_int_value() const { return int_value; }

	void add_dependency( IVP_U_Active_Int_Listener *derived_active_int );
	void remove_dependency( IVP_U_Active_Int_Listener *derived_active_int );

	virtual int print() = 0;

	explicit IVP_U_Active_Int( const char *name );
	virtual ~IVP_U_Active_Int();
};

/**** BASIC VALUES ********************/
/**** BASIC VALUES ********************/
/**** BASIC VALUES ********************/

// basic values are the ONLY values which can be changed directly.
// whenever they are changed, the change_meter is increased.

class IVP_U_Active_Terminal_Double : public IVP_U_Active_Float, public IVP_U_Active_Float_Delayed
{
private:
	IVP_DOUBLE old_value;

public:
	IVP_U_Active_Terminal_Double( const char *name, IVP_DOUBLE value );

	// tell dependent classes that something has changed
	void         update_float() override;
	virtual void set_double( IVP_DOUBLE new_value, IVP_BOOL delayed = IVP_FALSE );
	int          print() override;
};

class IVP_U_Active_Terminal_Int : public IVP_U_Active_Int, public IVP_U_Active_Int_Delayed
{
private:
	int old_value;

public:
	IVP_U_Active_Terminal_Int( const char *name, int value );

	void update_int() override; // Int::set_int

	virtual void set_int( int new_value, IVP_BOOL delayed = IVP_FALSE );
	int          print() override;
};

/*** OSCILLATORS  **************/
/*** OSCILLATORS  **************/
/*** OSCILLATORS  **************/

class IVP_U_Active_Sine : public IVP_U_Active_Float, public IVP_U_Active_Float_Listener
{
private:
	IVP_U_Active_Float *time_mod;  // depends on this
	IVP_DOUBLE          frequence; // Hz
	IVP_DOUBLE          amplitude;
	IVP_DOUBLE          null_level; // um diesen wert schwankt der sinus
	IVP_DOUBLE          time_shift;

public:
	IVP_U_Active_Sine( const char *name, IVP_U_Active_Float *time_mode,
	                   IVP_DOUBLE freq, IVP_DOUBLE amp, IVP_DOUBLE null_level,
	                   IVP_DOUBLE time_shift );
	~IVP_U_Active_Sine() override;

	void active_float_changed( IVP_U_Active_Float *calling_mod ) override;
	int  print() override;
};

class IVP_U_Active_Square : public IVP_U_Active_Float, public IVP_U_Active_Float_Listener
{
private:
	IVP_U_Active_Float *time_mod;  // depends on this
	IVP_DOUBLE          frequence; // Hz
	IVP_DOUBLE          low_val;
	IVP_DOUBLE          high_val;

public:
	IVP_U_Active_Square( const char *name, IVP_U_Active_Float *time_mod,
	                     IVP_DOUBLE freq, IVP_DOUBLE low_val, IVP_DOUBLE high_val );
	~IVP_U_Active_Square() override;

	void active_float_changed( IVP_U_Active_Float *calling_mod ) override;
	int  print() override;
};

class IVP_U_Active_Pulse : public IVP_U_Active_Float, public IVP_U_Active_Float_Listener
{
private:
	IVP_U_Active_Float *time_mod;  // depends on this
	IVP_DOUBLE          frequence; // Hz
	IVP_DOUBLE          low_val;
	IVP_DOUBLE          high_val;
	int                 m1; // m1 out of m2 defines interval prop.
	int                 m2;

public:
	IVP_U_Active_Pulse( const char *name, IVP_U_Active_Float *time_mod,
	                    IVP_DOUBLE freq,
	                    int        m1, // m1 out of m2 defines interval proportion
	                    int m2, IVP_DOUBLE low_val, IVP_DOUBLE high_val );
	~IVP_U_Active_Pulse() override;

	void active_float_changed( IVP_U_Active_Float *calling_mod ) override;
	int  print() override;
};

/***** MIXER ***********************************/
/***** MIXER ***********************************/
/***** MIXER ***********************************/

class IVP_U_Active_Add : public IVP_U_Active_Float, public IVP_U_Active_Float_Listener
{
	// adds two active_floats
private:
	IVP_U_Active_Float *mod0;
	IVP_U_Active_Float *mod1;

public:
	IVP_U_Active_Add( const char *name, IVP_U_Active_Float *mod0,
	                  IVP_U_Active_Float *mod1 );
	~IVP_U_Active_Add() override;

	void active_float_changed( IVP_U_Active_Float *calling_mod ) override;
	int  print() override;
};

class IVP_U_Active_Sub : public IVP_U_Active_Float, public IVP_U_Active_Float_Listener
{
	// subtracts two active_floats (mod0 - mod1)
private:
	IVP_U_Active_Float *mod0;
	IVP_U_Active_Float *mod1;

public:
	IVP_U_Active_Sub( const char *name, IVP_U_Active_Float *mod0,
	                  IVP_U_Active_Float *mod1 );
	~IVP_U_Active_Sub() override;

	void active_float_changed( IVP_U_Active_Float *calling_mod ) override;
	int  print() override;
};

class IVP_U_Active_Add_Multiple : public IVP_U_Active_Float, public IVP_U_Active_Float_Listener
{
	// adds a synthesizer active_IVP_FLOAT to another, using a factor
private:
	IVP_U_Active_Float *mod0;
	IVP_U_Active_Float *mod1;
	IVP_DOUBLE          factor;

public:
	IVP_U_Active_Add_Multiple( const char *name, IVP_U_Active_Float *mod0,
	                           IVP_U_Active_Float *mod1, IVP_DOUBLE factor );
	~IVP_U_Active_Add_Multiple() override;

	void active_float_changed( IVP_U_Active_Float *calling_mod ) override;
	int  print() override;
};

class IVP_U_Active_Mult : public IVP_U_Active_Float, public IVP_U_Active_Float_Listener
{
	// mults two synthesizer active_float
private:
	IVP_U_Active_Float *mod0;
	IVP_U_Active_Float *mod1;

public:
	IVP_U_Active_Mult( const char *name, IVP_U_Active_Float *mod0,
	                   IVP_U_Active_Float *mod1 );
	~IVP_U_Active_Mult() override;

	void active_float_changed( IVP_U_Active_Float *calling_mod ) override;
	int  print() override;
};

/**** FILTER **************************/
/**** FILTER **************************/
/**** FILTER **************************/

class IVP_U_Active_Limit : public IVP_U_Active_Float, public IVP_U_Active_Float_Listener
{
	// limits value range of a synthesizer active_float
private:
	IVP_U_Active_Float *mod;
	IVP_DOUBLE          low_val;
	IVP_DOUBLE          high_val;

public:
	IVP_U_Active_Limit( const char *name, IVP_U_Active_Float *mod,
	                    IVP_DOUBLE low_val, IVP_DOUBLE high_val );
	~IVP_U_Active_Limit() override;

	void active_float_changed( IVP_U_Active_Float *calling_mod ) override;
	int  print() override;
};

/********** LOGICS ****************/
/********** LOGICS ****************/
/********** LOGICS ****************/

class IVP_U_Active_Test_Range : public IVP_U_Active_Int, public IVP_U_Active_Float_Listener
{
	// tests range of a active_float
	// in range : 1
	// otherwise: 0

	// returns INTEGER value

private:
	IVP_U_Active_Float *mod_test;
	IVP_U_Active_Float *mod_low_val;
	IVP_U_Active_Float *mod_high_val;

public:
	IVP_U_Active_Test_Range( const char *name, IVP_U_Active_Float *mod_test,
	                         IVP_U_Active_Float *mod_low_val,
	                         IVP_U_Active_Float *mod_high_val );
	~IVP_U_Active_Test_Range() override;

	void active_float_changed( IVP_U_Active_Float *calling_mod ) override;
	int  print() override;
};

class IVP_U_Active_Switch : public IVP_U_Active_Float, public IVP_U_Active_Float_Listener, public IVP_U_Active_Int_Listener
{
	// if mod_cond is non-zero returns mod_true, else mod_false

	// returns DOUBLE value
	// depends on INT module!

private:
	IVP_U_Active_Int   *mod_cond;
	IVP_U_Active_Float *mod_true;
	IVP_U_Active_Float *mod_false;

public:
	IVP_U_Active_Switch( const char *name, IVP_U_Active_Int *mod_cond,
	                     IVP_U_Active_Float *mod_true,
	                     IVP_U_Active_Float *mod_false );
	~IVP_U_Active_Switch() override;

	void active_float_changed( IVP_U_Active_Float *calling_mod ) override;
	void active_int_changed( IVP_U_Active_Int *calling_mod ) override;
	int  print() override;
};

#endif