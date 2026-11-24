// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>
#include <ivu_vector.hxx>

// dimhotepus: Explicit includes for std::abort, std::memcpy, no reliance on the PCH.
#include <cstdlib>
#include <cstring>

void IVP_U_Vector_Base::increment_mem()
{
	// dimhotepus: Was IVP_ASSERT only, compiled out in release. At 0xFFFF
	// capacity the old code kept going, then n_elems++ wrapped the unsigned short
	// to 0 and the vector silently lost all its elements. Cannot report an error
	// from here, so fail fast instead of corrupting state.
	if ( memsize == 0xFFFF )
	{
		IVP_ASSERT( !"IVP_U_Vector: capacity overflow (max 65535 elements)" );
		std::abort();
	}
	IVP_ASSERT( n_elems <= memsize );

	// dimhotepus: Compute new capacity once, clamped to what unsigned short can
	// hold. Old code allocated 2 * ( memsize + 1 ) slots but stored
	// min( memsize * 2 + 1, 0xFFFF ), so allocation and memsize disagreed.
	const unsigned int doubled = (unsigned int)memsize * 2u + 1u;
	const unsigned int new_size = doubled > 0xFFFFu ? 0xFFFFu : doubled;
	// dimhotepus: size_t math, defined on 32-bit and 64-bit targets.
	const size_t new_bytes = (size_t)new_size * sizeof( void * );

	// dimhotepus: Preallocated buffer belongs to the derived object and must
	// never be passed to realloc / free. GEKKO p_realloc is a stub (asserts and
	// returns 0), so there we always take the malloc + memcpy + free path.
#ifndef GEKKO
	const bool can_realloc = !is_inline_storage();
#else
	const bool can_realloc = false;
#endif

	void **new_elems;

	if ( can_realloc )
	{
		// dimhotepus: realloc extends in place when possible (no copy, no free)
		// and copies only on move. Also handles elems == nullptr (memsize == 0)
		// as plain malloc.
		new_elems = (void **)p_realloc( elems, new_bytes );

		// dimhotepus: On failure realloc returns nullptr, fail fast instead of
		// writing through a null pointer.
		if ( !new_elems )
		{
			IVP_ASSERT( !"IVP_U_Vector: p_realloc failed" );
			std::abort();
		}
	}
	else
	{
		new_elems = (void **)p_malloc( new_bytes );

		// dimhotepus: One memcpy instead of per-element loop, faster, and
		// skipped when empty (memcpy from nullptr is UB even for size 0).
		if ( n_elems > 0 )
		{
			std::memcpy( new_elems, elems, (size_t)n_elems * sizeof( void * ) );
		}

		// dimhotepus: Shared helper instead of open-coded this + 1 compare.
		// elems may be nullptr here only on GEKKO, guard it.
		if ( elems && !is_inline_storage() )
		{
			p_free( elems );
		}
	}

	elems = new_elems;
	memsize = (unsigned short)new_size;
}
