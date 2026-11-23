// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

// IVP_U_Active_Float ist ein modular kombinierbarer Zahlengeber (Synthesizer)
// und Event-Ausloeser zur Implementierung der Spiel-Logik ausserhalb
// der eigentlichen Grundphysik.
// z.B. fuer IVP_Actuator_Forces (oder Klaenge :-)

// IVP_Active_Float is a modular combinable number generator to implement
// some logics stuff. It is no internal part of the physics but some
// add on for convenience. It is based on event-callbacks to update the
// values when it is necessary. It is used e.g. for IVP_Actuator_Forces.

#include <ivp_physics.hxx>
#include <ivu_active_value.hxx>
#include <ivu_active_value_hash.hxx>

#include <climits>
#include <cstddef>
#include <cstring>

namespace
{
// dimhotepus: (int)double is UB for NaN and out-of-range values. Saturate
// instead, NaN -> 0.
[[nodiscard]] inline int ivp_active_double_to_int( IVP_DOUBLE v )
{
	if ( !( v > static_cast< IVP_DOUBLE >( INT_MIN ) ) )
		return v != v ? 0 : INT_MIN;
	if ( v >= static_cast< IVP_DOUBLE >( INT_MAX ) )
		return INT_MAX;
	return static_cast< int >( v );
}

// dimhotepus: The "name is a number" test was copy-pasted in two functions.
[[nodiscard]] inline bool ivp_active_name_is_number( const char *n )
{
	int pos = 0;
	if ( n[pos] == '-' )
		pos++; // for negative sign
	if ( n[pos] == '.' )
		pos++; // for omitted leading zero
	return n[pos] >= '0' && n[pos] <= '9';
}
} // namespace

// ----------------------------------------------------------------------------
// IVP_Active_Value_Hash
// ----------------------------------------------------------------------------

IVP_Active_Value_Hash::~IVP_Active_Value_Hash()
{
	for ( int i = this->len() - 1; i >= 0; i-- )
	{
		IVP_U_Active_Value *av;
		av = (IVP_U_Active_Value *)this->element_at( i );
		if ( av )
		{
			av->remove_reference();
		}
	}
}

int IVP_Active_Value_Hash::object_to_index( IVP_U_Active_Value *av )
{
	const char *name = av->get_name();
	// dimhotepus: std::strlen( nullptr ) is UB. A null name hashes as "".
	if ( !name )
		name = "";
	// dimhotepus: hash_index takes hk_intp, no narrowing to int.
	return hash_index( name, static_cast< hk_intp >( std::strlen( name ) ) );
}

IVP_BOOL IVP_Active_Value_Hash::compare( const void *elem0,
                                         const void *elem1 ) const
{
	const IVP_U_Active_Value *av0 = (const IVP_U_Active_Value *)elem0;
	const IVP_U_Active_Value *av1 = (const IVP_U_Active_Value *)elem1;

	const char *name0 = av0->get_name();
	const char *name1 = av1->get_name();
	// dimhotepus: Null names (the manager's probe value) never reach strcmp.
	if ( !name0 || !name1 )
		return name0 == name1 ? IVP_TRUE : IVP_FALSE;
	if ( p_strcmp( name0, name1 ) == 0 )
		return IVP_TRUE;
	return IVP_FALSE;
}

void IVP_Active_Value_Hash::add_active_value( IVP_U_Active_Value *av )
{
	add_elem( av, object_to_index( av ) );
	av->add_reference();
}

bool IVP_Active_Value_Hash::remove_active_value( IVP_U_Active_Value *av )
{
	IVP_U_Active_Value *av_out =
	(IVP_U_Active_Value *)remove_elem( av, object_to_index( av ) );
	// dimhotepus: The old code dereferenced av_out unconditionally, which
	// crashed when nothing was found.
	if ( !av_out )
		return false;
	av_out->remove_reference(); // may delete av_out
	return true;
}

IVP_U_Active_Value *IVP_Active_Value_Hash::find_active_value( IVP_U_Active_Value *av )
{
	return (IVP_U_Active_Value *)find_elem( av, object_to_index( av ) );
}

// ----------------------------------------------------------------------------
// IVP_U_Active_Value_Manager
// ----------------------------------------------------------------------------

// dimhotepus: Was int change_meter = -1. Unsigned so the per-set increment can
// wrap without UB. The value (0xFFFFFFFF) and the first increment to 0 behave
// exactly as -1 -> 0 did.
unsigned int IVP_U_Active_Float::change_meter = static_cast< unsigned int >( -1 );

IVP_U_Active_Value_Manager::IVP_U_Active_Value_Manager(
IVP_BOOL delete_on_env_delete_in )
{
	delete_on_env_delete = delete_on_env_delete_in;
	// init name hash
	this->floats_name_hash = new IVP_Active_Value_Hash( IVP_U_MOD_NAME_HASH_SIZE );
	this->ints_name_hash = new IVP_Active_Value_Hash( IVP_U_MOD_NAME_HASH_SIZE );
	this->search_active_value = new IVP_U_Active_Value( nullptr );
	this->mod_current_time = nullptr;
	// Note: virtual call in a constructor. Overrides in derived managers are not
	// reached from here.
	this->init_active_values_generic();
}

IVP_U_Active_Value_Manager::~IVP_U_Active_Value_Manager()
{
	P_DELETE( this->floats_name_hash ); // removes references two
	P_DELETE( this->ints_name_hash );
	P_DELETE( this->search_active_value );
	// dimhotepus: The hash just released (and probably deleted) this value.
	this->mod_current_time = nullptr;
}

IVP_U_Active_Value *IVP_U_Active_Value_Manager::find_named(
IVP_Active_Value_Hash *hash, const char *name )
{
	if ( !name )
		return nullptr;
	// The probe value borrows the caller's string for the lookup only.
	search_active_value->name = name; // bad hack, thats ok
	IVP_U_Active_Value *found = hash->find_active_value( search_active_value );
	search_active_value->name = nullptr;
	return found;
}

void IVP_U_Active_Value_Manager::init_active_values_generic()
{
	auto *time = new IVP_U_Active_Terminal_Double(
	IVP_ACTIVE_FLOAT_CURRENT_TIME_NAME, 0.0f );
	auto *null = new IVP_U_Active_Terminal_Double( "double_null", 0.0f );
	// dimhotepus: Do not leak insert fails.
	if ( !this->insert_active_float( null ) )
	{
		delete null;
	}
	// dimhotepus: This insert was unchecked and mod_current_time was assigned
	// before it. If it failed (init called twice), the new value leaked and
	// mod_current_time pointed at an unowned object.
	if ( this->insert_active_float( time ) )
	{
		this->mod_current_time = time;
	}
	else
	{
		delete time;
	}
}

// dimhotepus: void -> bool.
bool IVP_U_Active_Value_Manager::insert_active_float( IVP_U_Active_Float *mod )
{
	const char *name = mod->get_name();
	// dimhotepus: p_strlen( nullptr ) is UB for a nameless value.
	if ( !name || !*name )
	{
		ivp_message(
		"insert_active_float: tried to insert active_IVP_FLOAT without "
		"name!\n" );
		return false;
	}

	if ( find_named( floats_name_hash, name ) )
	{
		ivp_message( "insert_active_float: name '%s' already exists in name_hash!\n",
		             name );
		return false;
	}

	floats_name_hash->add_active_value( mod );
	mod->l_mod_manager = this; // provide backlink

	return true;
}

bool IVP_U_Active_Value_Manager::insert_active_int( IVP_U_Active_Int *mod )
{
	const char *name = mod->get_name();
	if ( !name || !*name )
	{
		ivp_message(
		"insert_active_int: tried to insert active_int without name!\n" );
		return false;
	}

	if ( find_named( ints_name_hash, name ) )
	{
		ivp_message( "insert_active_int: name '%s' already exists in name_hash!\n",
		             name );
		return false;
	}

	// dimhotepus: Removed the extra mod->add_reference(). add_active_value
	// already takes the hash's reference, and the hash destructor releases
	// exactly one. The extra one made int values leak. Floats never had it.
	ints_name_hash->add_active_value( mod );

	mod->l_mod_manager = this; // provide backlink
	return true;
}

void IVP_U_Active_Value_Manager::remove_active_float( IVP_U_Active_Float *mod )
{
	floats_name_hash->remove_active_value( mod );
}

void IVP_U_Active_Value_Manager::remove_active_int( IVP_U_Active_Int *mod )
{
	ints_name_hash->remove_active_value( mod );
}

void IVP_U_Active_Value_Manager::delay_active_float(
IVP_U_Active_Float_Delayed *mod )
{
	if ( delayed_active_floats.index_of( mod ) == -1 )
	{
		delayed_active_floats.add( mod );
	}
}

void IVP_U_Active_Value_Manager::delay_active_int(
IVP_U_Active_Int_Delayed *mod )
{
	if ( delayed_active_ints.index_of( mod ) == -1 )
	{
		delayed_active_ints.add( mod );
	}
}

void IVP_U_Active_Value_Manager::update_delayed_active_values()
{
	// update all active_floats contained in vector delayed_active_floats
	{
		IVP_U_Active_Float_Delayed *mod;
		int                         i;
		for ( i = 0; i < delayed_active_floats.len(); i++ )
		{
			mod = delayed_active_floats.element_at( i );
			mod->update_float();
		}
		delayed_active_floats.clear();
	}

	{
		IVP_U_Active_Int_Delayed *mod;
		int                       i;
		for ( i = 0; i < delayed_active_ints.len(); i++ )
		{
			mod = delayed_active_ints.element_at( i );
			mod->update_int();
		}
		delayed_active_ints.clear();
	}
}

void IVP_U_Active_Value_Manager::refresh_psi_active_values(
IVP_Environment *env )
{
	if ( mod_current_time )
	{
		mod_current_time->set_double( env->get_current_time().get_time() );
	}
	update_delayed_active_values();
}

IVP_U_Active_Float *IVP_U_Active_Value_Manager::get_active_float_by_name(
const char *i_name )
{
	if ( !i_name )
		return nullptr;
	// check module hash first
	if ( auto *mod = static_cast< IVP_U_Active_Float * >( find_named( floats_name_hash, i_name ) ) )
		return mod;

	// if module name is a number, generate terminal active_floats with
	// that (IVP_DOUBLE) value on the fly.
	if ( ivp_active_name_is_number( i_name ) )
	{
		auto *mod = new IVP_U_Active_Terminal_Double( i_name, p_atof( i_name ) );
		// dimhotepus: Do not leak or return an unowned value if insert fails.
		if ( !this->insert_active_float( mod ) )
		{
			delete mod;
			return nullptr;
		}
		return mod;
	}
	return nullptr;
}

IVP_U_Active_Int *IVP_U_Active_Value_Manager::get_active_int_by_name(
const char *i_name )
{
	if ( !i_name )
		return nullptr;
	// check module hash first
	if ( auto *mod = static_cast< IVP_U_Active_Int * >( find_named( ints_name_hash, i_name ) ) )
		return mod;

	// if module name is a number, generate terminal active_ints with
	// that int value on the fly.
	if ( ivp_active_name_is_number( i_name ) )
	{
		auto *mod = new IVP_U_Active_Terminal_Int( i_name, p_atoi( i_name ) );
		if ( !this->insert_active_int( mod ) )
		{
			delete mod;
			return nullptr;
		}
		return mod;
	}
	return nullptr;
}

IVP_U_Active_Float *IVP_U_Active_Value_Manager::install_active_float(
const char *i_name, IVP_DOUBLE value )
{
	// dimhotepus: A null or empty name can never be inserted. Previously the new
	// value was created, the insert failed, and the unowned value was returned.
	if ( !i_name || !*i_name )
		return nullptr;

	// check module hash first
	if ( auto *mod = static_cast< IVP_U_Active_Float * >( find_named( floats_name_hash, i_name ) ) )
		return mod;

	auto *atd = new IVP_U_Active_Terminal_Double( i_name, value );
	if ( !this->insert_active_float( atd ) )
	{
		delete atd;
		return nullptr;
	}
	return atd;
}

IVP_U_Active_Terminal_Double *IVP_U_Active_Value_Manager::create_active_float(
const char *i_name, IVP_DOUBLE value )
{
	if ( !i_name || !*i_name )
		return nullptr;

	// check module hash first
	if ( find_named( floats_name_hash, i_name ) )
		return nullptr;

	auto *atd = new IVP_U_Active_Terminal_Double( i_name, value );
	if ( !this->insert_active_float( atd ) )
	{
		delete atd;
		return nullptr;
	}
	return atd;
}

IVP_U_Active_Terminal_Int *IVP_U_Active_Value_Manager::create_active_int(
const char *i_name, int value )
{
	if ( !i_name || !*i_name )
		return nullptr;

	// check module hash first
	if ( find_named( ints_name_hash, i_name ) )
		return nullptr;

	auto *atd = new IVP_U_Active_Terminal_Int( i_name, value );
	if ( !this->insert_active_int( atd ) )
	{
		delete atd;
		return nullptr;
	}
	return atd;
}

IVP_U_Active_Int *IVP_U_Active_Value_Manager::install_active_int(
const char *i_name, int value )
{
	if ( !i_name || !*i_name )
		return nullptr;

	// check module hash first
	if ( auto *mod = static_cast< IVP_U_Active_Int * >( find_named( ints_name_hash, i_name ) ) )
		return mod;

	auto *atd = new IVP_U_Active_Terminal_Int( i_name, value );
	if ( !this->insert_active_int( atd ) )
	{
		delete atd;
		return nullptr;
	}
	return atd;
}

//////////////
IVP_U_Active_Value::IVP_U_Active_Value( const char *name_ )
{
	// dimhotepus: The manager's probe value is created with a null name, and
	// p_strdup( nullptr ) is not guaranteed to be safe.
	name = name_ ? p_strdup( name_ ) : nullptr;
	reference_count = 0;
}

IVP_U_Active_Value::~IVP_U_Active_Value()
{
	P_FREE( name );
	IVP_ASSERT( reference_count == 0 );
}

IVP_U_Active_Float::IVP_U_Active_Float( const char *i_name ) :
	IVP_U_Active_Value( i_name )
{
	// memclear not allowed because of virt. functions
	this->last_update = 0;         // should be updated
	this->l_mod_manager = nullptr; // link is done by manager
	this->double_value = 0.0f;
}

IVP_U_Active_Int::IVP_U_Active_Int( const char *i_name ) :
	IVP_U_Active_Value( i_name )
{
	// memclear not allowed because of virt. functions
	this->last_update = 0;         // should be updated
	this->l_mod_manager = nullptr; // link is done by manager
	this->int_value = 0;
}

IVP_U_Active_Float::~IVP_U_Active_Float()
{
	// if(name_hash){
	//     name_hash->remove(this->name);
	// }
	// must be done by hand (or by manager), no arg supported by destructor !
}

IVP_U_Active_Int::~IVP_U_Active_Int()
{
	// if(name_hash){
	//     name_hash->remove(this->name);
	// }
	// must be done by hand (or by manager), no arg supported by destructor !
}

void IVP_U_Active_Float::update_derived()
{
	// update all active_float_listener which depend on my value

	for ( int i = derived_mods.len() - 1; i >= 0; i-- )
	{
		IVP_U_Active_Float_Listener *mod = derived_mods.element_at( i );
		mod->active_float_changed( this );
	}
}

void IVP_U_Active_Int::update_derived()
{
	// update all active_int_listener which depend on my value

	for ( int i = derived_mods.len() - 1; i >= 0; i-- )
	{
		IVP_U_Active_Int_Listener *mod = derived_mods.element_at( i );
		mod->active_int_changed( this );
	}
}

void IVP_U_Active_Float::add_dependency(
IVP_U_Active_Float_Listener *derived_active_IVP_FLOAT )
{
	// add update callback
	this->derived_mods.add( derived_active_IVP_FLOAT );
	this->add_reference();
}

void IVP_U_Active_Float::remove_dependency(
IVP_U_Active_Float_Listener *derived_active_IVP_FLOAT )
{
	// remove update callback
	this->derived_mods.remove( derived_active_IVP_FLOAT );
	this->remove_reference(); // may delete this: do not touch members after
}

void IVP_U_Active_Int::add_dependency(
IVP_U_Active_Int_Listener *derived_active_int )
{
	// add update callback
	this->derived_mods.add( derived_active_int );
	this->add_reference();
}

void IVP_U_Active_Int::remove_dependency(
IVP_U_Active_Int_Listener *derived_active_int )
{
	// remove update callback
	this->derived_mods.remove( derived_active_int );
	this->remove_reference(); // may delete this: do not touch members after
}

/**** BASIC VALUES ********************/
/**** BASIC VALUES ********************/
/**** BASIC VALUES ********************/

/****** 'terminal' IVP_DOUBLE value ******/

IVP_U_Active_Terminal_Double::IVP_U_Active_Terminal_Double( const char *i_name,
                                                            IVP_DOUBLE  new_value ) :
	IVP_U_Active_Float( i_name )
{
	this->double_value = new_value;
	this->old_value = new_value;
}

IVP_U_Active_Terminal_Int::IVP_U_Active_Terminal_Int( const char *i_name,
                                                      int         new_value ) :
	IVP_U_Active_Int( i_name )
{
	this->int_value = new_value;
	this->old_value = new_value;
}

void IVP_U_Active_Terminal_Double::update_float()
{
	if ( !hk_Math::almost_equal( this->double_value, old_value ) )
	{
		this->old_value = double_value;
		this->update_derived();
	}
}

void IVP_U_Active_Terminal_Int::update_int()
{
	if ( this->int_value != old_value )
	{
		this->old_value = int_value;
		this->update_derived();
	}
}

void IVP_U_Active_Terminal_Double::set_double( IVP_DOUBLE new_value,
                                               IVP_BOOL   delayed_update )
{
	this->double_value = new_value;
	IVP_U_Active_Float::change_meter++;
	if ( delayed_update && l_mod_manager )
	{
		l_mod_manager->delay_active_float( this );
	}
	else
	{
		this->update_float();
	}
}

int IVP_U_Active_Terminal_Double::print()
{
	ivp_message( "DoubleVal" );
	return 0; // for debugger
}

////////////////////

void IVP_U_Active_Terminal_Int::set_int( int      new_value,
                                         IVP_BOOL delayed_update )
{
	this->int_value = new_value;
	IVP_U_Active_Float::change_meter++;

	if ( delayed_update && l_mod_manager )
	{
		l_mod_manager->delay_active_int( this );
	}
	else
	{
		this->update_int();
	}
}

int IVP_U_Active_Terminal_Int::print()
{
	ivp_message( "IntVal" );
	return 0; // for debugger
}

/*** OSCILLATORS (time dependant) **************/
/*** OSCILLATORS (time dependant) **************/
/*** OSCILLATORS (time dependant) **************/

IVP_U_Active_Sine::IVP_U_Active_Sine( const char         *i_name,
                                      IVP_U_Active_Float *time_mod_,
                                      IVP_DOUBLE freq, IVP_DOUBLE amp,
                                      IVP_DOUBLE i_null_level,
                                      IVP_DOUBLE i_time_shift ) :
	IVP_U_Active_Float( i_name )
{
	this->time_mod = time_mod_;

	this->time_mod->add_dependency( this );

	this->frequence = freq;
	this->amplitude = amp;
	this->null_level = i_null_level;
	this->time_shift = i_time_shift;

	this->active_float_changed( this ); // no derived existing yet!
}

IVP_U_Active_Sine::~IVP_U_Active_Sine()
{
	this->time_mod->remove_dependency( this );
}

void IVP_U_Active_Sine::active_float_changed( IVP_U_Active_Float * )
{
	if ( this->last_update == this->change_meter )
		return; // already valid
	this->last_update = this->change_meter;

	IVP_DOUBLE time = time_mod->give_double_value();
	IVP_DOUBLE new_val =
	IVP_Inline_Math::sind( time * frequence + time_shift ) * amplitude +
	null_level;

	if ( !hk_Math::almost_equal( new_val, this->double_value ) )
	{
		this->double_value = new_val;
		this->update_derived();
	}
}

int IVP_U_Active_Sine::print()
{
	ivp_message( "Sine[F %g, A %g, N %g, ts %g](", frequence, amplitude,
	             null_level, time_shift );
	time_mod->print();
	ivp_message( ")" );
	return 0; // for debugger
}

///////////////////

IVP_U_Active_Square::IVP_U_Active_Square( const char         *i_name,
                                          IVP_U_Active_Float *i_time_mod,
                                          IVP_DOUBLE          i_freq,
                                          IVP_DOUBLE          i_low_val,
                                          IVP_DOUBLE          i_high_val ) :
	IVP_U_Active_Float( i_name )
{
	this->time_mod = i_time_mod;

	time_mod->add_dependency( this );

	this->frequence = i_freq;
	this->low_val = i_low_val;
	this->high_val = i_high_val;
}

IVP_U_Active_Square::~IVP_U_Active_Square()
{
	time_mod->remove_dependency( this );
}

void IVP_U_Active_Square::active_float_changed( IVP_U_Active_Float * )
{
	if ( last_update == change_meter )
		return; // already valid
	last_update = change_meter;

	IVP_DOUBLE time = time_mod->give_double_value();
	// dimhotepus: Saturating conversion. (int)double is UB for NaN / huge values.
	int        h = ivp_active_double_to_int( time * frequence ) & 1;
	IVP_DOUBLE new_val = ( h ) ? high_val : low_val;

	if ( !hk_Math::almost_equal( new_val, this->double_value ) )
	{
		this->double_value = new_val;
		this->update_derived();
	}
}

int IVP_U_Active_Square::print()
{
	ivp_message( "Square[F %g, L %g, H %g](", frequence, low_val, high_val );
	time_mod->print();
	ivp_message( ")" );
	return 0; // f db
}

/////////////////

IVP_U_Active_Pulse::IVP_U_Active_Pulse( const char         *i_name,
                                        IVP_U_Active_Float *i_time_mod,
                                        IVP_DOUBLE freq, int i_m1, int i_m2,
                                        IVP_DOUBLE i_low_val,
                                        IVP_DOUBLE i_high_val ) :
	IVP_U_Active_Float( i_name )
{
	// dimhotepus: m2 is a modulus. Zero (or negative) made "% m2" divide by zero.
	IVP_ASSERT( i_m2 > 0 );

	this->time_mod = i_time_mod;

	time_mod->add_dependency( this );

	this->frequence = freq;
	this->low_val = i_low_val;
	this->high_val = i_high_val;
	this->m1 = i_m1;
	this->m2 = i_m2;

	this->active_float_changed( this );
}

IVP_U_Active_Pulse::~IVP_U_Active_Pulse()
{
	time_mod->remove_dependency( this );
}

void IVP_U_Active_Pulse::active_float_changed( IVP_U_Active_Float * )
{
	if ( last_update == change_meter )
		return; // already valid
	last_update = change_meter;

	IVP_DOUBLE time = time_mod->give_double_value();
	// dimhotepus: Release builds skip the assert, so guard the modulus too.
	// Saturating conversion, see Square.
	const int  val = m2 > 0 ? ivp_active_double_to_int( time * frequence * m2 ) % m2 : m1;
	IVP_DOUBLE new_val = ( val < m1 ) ? high_val : low_val;

	if ( !hk_Math::almost_equal( new_val, this->double_value ) )
	{
		this->double_value = new_val;
		this->update_derived();
	}
}

int IVP_U_Active_Pulse::print()
{
	ivp_message( "Pulse[F %g, L %g, H %g, %d:%d](", frequence, low_val, high_val,
	             m1, m2 );
	time_mod->print();
	ivp_message( ")" );
	return 0; // f db
}

/***** MIXER ***********************************/
/***** MIXER ***********************************/
/***** MIXER ***********************************/

IVP_U_Active_Add::IVP_U_Active_Add( const char         *i_name,
                                    IVP_U_Active_Float *i_mod0,
                                    IVP_U_Active_Float *i_mod1 ) :
	IVP_U_Active_Float( i_name )
{
	this->mod0 = i_mod0;
	this->mod1 = i_mod1;

	mod0->add_dependency( this );
	mod1->add_dependency( this );

	this->active_float_changed( this );
}

IVP_U_Active_Add::~IVP_U_Active_Add()
{
	mod0->remove_dependency( this );
	mod1->remove_dependency( this );
}

void IVP_U_Active_Add::active_float_changed( IVP_U_Active_Float * )
{
	if ( last_update == change_meter )
		return; // already valid
	last_update = change_meter;

	IVP_DOUBLE new_val = mod0->give_double_value() + mod1->give_double_value();

	if ( !hk_Math::almost_equal( new_val, this->double_value ) )
	{
		this->double_value = new_val;
		this->update_derived();
	}
}

int IVP_U_Active_Add::print()
{
	mod0->print();
	ivp_message( " + " );
	mod1->print();

	return 0; // for debugger
}

///////////////////////

IVP_U_Active_Sub::IVP_U_Active_Sub( const char         *i_name,
                                    IVP_U_Active_Float *i_mod0,
                                    IVP_U_Active_Float *i_mod1 ) :
	IVP_U_Active_Float( i_name )
{
	this->mod0 = i_mod0;
	this->mod1 = i_mod1;

	mod0->add_dependency( this );
	mod1->add_dependency( this );

	this->active_float_changed( this );
}

IVP_U_Active_Sub::~IVP_U_Active_Sub()
{
	mod0->remove_dependency( this );
	mod1->remove_dependency( this );
}

void IVP_U_Active_Sub::active_float_changed( IVP_U_Active_Float * )
{
	if ( last_update == change_meter )
		return; // already valid
	last_update = change_meter;

	IVP_DOUBLE new_val = mod0->give_double_value() - mod1->give_double_value();

	if ( !hk_Math::almost_equal( new_val, this->double_value ) )
	{
		this->double_value = new_val;
		this->update_derived();
	}
}

int IVP_U_Active_Sub::print()
{
	mod0->print();
	ivp_message( " - " );
	mod1->print();

	return 0; // for debugger
}

///////////////////////

IVP_U_Active_Add_Multiple::IVP_U_Active_Add_Multiple( const char         *i_name,
                                                      IVP_U_Active_Float *i_mod0,
                                                      IVP_U_Active_Float *i_mod1,
                                                      IVP_DOUBLE          i_factor ) :
	IVP_U_Active_Float( i_name )
{
	this->mod0 = i_mod0;
	this->mod1 = i_mod1;

	this->factor = i_factor;

	mod0->add_dependency( this );
	mod1->add_dependency( this );

	// dimhotepus: Dropped the redundant (IVP_U_Active_Float *) cast of this.
	this->active_float_changed( this );
}

IVP_U_Active_Add_Multiple::~IVP_U_Active_Add_Multiple()
{
	mod0->remove_dependency( this );
	mod1->remove_dependency( this );
}

void IVP_U_Active_Add_Multiple::active_float_changed( IVP_U_Active_Float * )
{
	if ( last_update == change_meter )
		return; // already valid
	last_update = change_meter;

	IVP_DOUBLE new_val =
	mod0->give_double_value() + factor * mod1->give_double_value();

	if ( !hk_Math::almost_equal( new_val, this->double_value ) )
	{
		this->double_value = new_val;
		this->update_derived();
	}
}

int IVP_U_Active_Add_Multiple::print()
{
	ivp_message( "(" );
	mod0->print();
	ivp_message( ")" );

	ivp_message( " + %g * (", factor );
	mod1->print();
	ivp_message( ")" );

	return 0; // f db
}

////////////////////

IVP_U_Active_Mult::IVP_U_Active_Mult( const char         *i_name,
                                      IVP_U_Active_Float *i_mod0,
                                      IVP_U_Active_Float *i_mod1 ) :
	IVP_U_Active_Float( i_name )
{
	this->mod0 = i_mod0;
	this->mod1 = i_mod1;

	mod0->add_dependency( this );
	mod1->add_dependency( this );

	this->active_float_changed( this );
}

IVP_U_Active_Mult::~IVP_U_Active_Mult()
{
	mod0->remove_dependency( this );
	mod1->remove_dependency( this );
}

void IVP_U_Active_Mult::active_float_changed( IVP_U_Active_Float * )
{
	if ( last_update == change_meter )
		return; // already valid
	last_update = change_meter;

	IVP_DOUBLE new_val = mod0->give_double_value() * mod1->give_double_value();

	if ( !hk_Math::almost_equal( new_val, this->double_value ) )
	{
		this->double_value = new_val;
		this->update_derived();
	}
}

int IVP_U_Active_Mult::print()
{
	ivp_message( "(" );
	mod0->print();
	ivp_message( ")" );

	ivp_message( " * " );

	ivp_message( "(" );
	mod1->print();
	ivp_message( ")" );

	return 0; // f db
}

/**** FILTER **************************/
/**** FILTER **************************/
/**** FILTER **************************/

IVP_U_Active_Limit::IVP_U_Active_Limit( const char         *i_name,
                                        IVP_U_Active_Float *i_mod,
                                        IVP_DOUBLE          i_low_val,
                                        IVP_DOUBLE          i_high_val ) :
	IVP_U_Active_Float( i_name )
{
	this->mod = i_mod;

	this->low_val = i_low_val;
	this->high_val = i_high_val;

	mod->add_dependency( this );

	this->active_float_changed( this );
}

IVP_U_Active_Limit::~IVP_U_Active_Limit()
{
	mod->remove_dependency( this );
}

void IVP_U_Active_Limit::active_float_changed( IVP_U_Active_Float * )
{
	if ( last_update == change_meter )
		return; // already valid
	last_update = change_meter;

	IVP_DOUBLE new_val = mod->give_double_value();
	if ( new_val < low_val )
		new_val = low_val;
	if ( new_val > high_val )
		new_val = high_val;

	if ( !hk_Math::almost_equal( new_val, this->double_value ) )
	{
		this->double_value = new_val;
		this->update_derived();
	}
}

int IVP_U_Active_Limit::print()
{
	ivp_message( "Limit[%g, %g](", low_val, high_val );
	mod->print();
	ivp_message( ")" );
	return 0; // for debugger
}

/********** LOGICS ****************/
/********** LOGICS ****************/
/********** LOGICS ****************/

IVP_U_Active_Test_Range::IVP_U_Active_Test_Range(
const char *i_name, IVP_U_Active_Float *i_mod_test,
IVP_U_Active_Float *i_mod_low_val, IVP_U_Active_Float *i_mod_high_val ) :
	IVP_U_Active_Int( i_name )
{
	this->mod_test = i_mod_test;

	this->mod_low_val = i_mod_low_val;

	this->mod_high_val = i_mod_high_val;

	mod_test->add_dependency( this );
	mod_low_val->add_dependency( this );
	mod_high_val->add_dependency( this );

	// dimhotepus: Was (IVP_U_Active_Float *)this. This class derives from
	// IVP_U_Active_Int, not IVP_U_Active_Float, so that cast produced a pointer
	// to nothing valid. The argument is unused, so pass nullptr like
	// IVP_U_Active_Switch::active_int_changed does.
	this->active_float_changed( nullptr );
}

IVP_U_Active_Test_Range::~IVP_U_Active_Test_Range()
{
	mod_test->remove_dependency( this );
	mod_low_val->remove_dependency( this );
	mod_high_val->remove_dependency( this );
}

void IVP_U_Active_Test_Range::active_float_changed( IVP_U_Active_Float * )
{
	if ( last_update == IVP_U_Active_Float::change_meter )
		return; // already valid
	last_update = IVP_U_Active_Float::change_meter;

	IVP_DOUBLE cmp_val = mod_test->give_double_value();
	IVP_DOUBLE low_val = mod_low_val->give_double_value();
	IVP_DOUBLE high_val = mod_high_val->give_double_value();
	int        new_val = ( ( cmp_val >= low_val ) && ( cmp_val <= high_val ) ) ? 1 : 0;
	if ( new_val != this->int_value )
	{
		this->int_value = new_val;
		this->update_derived();
	}
}

int IVP_U_Active_Test_Range::print()
{
	ivp_message( "TestRange[" );
	mod_low_val->print();
	ivp_message( ", " );
	mod_high_val->print();
	ivp_message( "](" );
	mod_test->print();
	ivp_message( ")" );
	return 0; // fdb
}

////////////////

IVP_U_Active_Switch::IVP_U_Active_Switch( const char         *i_name,
                                          IVP_U_Active_Int   *i_mod_cond,
                                          IVP_U_Active_Float *i_mod_true,
                                          IVP_U_Active_Float *i_mod_false ) :
	IVP_U_Active_Float( i_name )
{
	this->mod_cond = i_mod_cond;

	this->mod_true = i_mod_true;
	this->mod_false = i_mod_false;

	mod_cond->add_dependency( this );
	mod_true->add_dependency( this );
	mod_false->add_dependency( this );

	this->active_float_changed( this );
}

IVP_U_Active_Switch::~IVP_U_Active_Switch()
{
	mod_cond->remove_dependency( this );
	mod_true->remove_dependency( this );
	mod_false->remove_dependency( this );
}

void IVP_U_Active_Switch::active_float_changed( IVP_U_Active_Float * )
{
	if ( last_update == IVP_U_Active_Float::change_meter )
		return; // already valid
	last_update = IVP_U_Active_Float::change_meter;

	// dimhotepus: The condition is an int. Compare it directly instead of
	// converting to double for almost_zero.
	const IVP_DOUBLE new_val = mod_cond->give_int_value() != 0
	                           ? mod_true->give_double_value()
	                           : mod_false->give_double_value();

	if ( !hk_Math::almost_equal( new_val, this->double_value ) )
	{
		this->double_value = new_val;
		this->update_derived();
	}
}

void IVP_U_Active_Switch::active_int_changed( IVP_U_Active_Int * )
{
	active_float_changed( nullptr );
}

int IVP_U_Active_Switch::print()
{
	ivp_message( "Cond[" );
	mod_true->print();
	ivp_message( ", " );
	mod_false->print();
	ivp_message( "](" );
	mod_cond->print();
	ivp_message( ")" );
	return 0; // fdb
}