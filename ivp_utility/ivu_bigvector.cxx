// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>

#include <ivu_bigvector.hxx>

// dimhotepus: Explicit includes for INT_MAX, SIZE_MAX, memcpy, std::abort,
// no reliance on the PCH.
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <cstring>

void IVP_U_BigVector_Base::increment_mem()
{
	// dimhotepus: Sanity check of invariants before touching memory.
	IVP_ASSERT( memsize >= 0 && n_elems >= 0 && n_elems <= memsize );

	// dimhotepus: Old code never checked overflow. memsize * 2 + 1 overflows
	// signed int (UB) and the byte size can wrap on 32-bit size_t (x86), giving
	// a tiny allocation and a heap overwrite. Limit is the smaller of what fits
	// in int and what fits in size_t bytes, so it is right on 32-bit and 64-bit.
	constexpr size_t byte_limit = SIZE_MAX / sizeof( void * );
	constexpr size_t max_elems = byte_limit < (size_t)INT_MAX ? byte_limit : (size_t)INT_MAX;

	if ( (size_t)memsize > ( max_elems - 1 ) / 2 )
	{
		IVP_ASSERT( !"IVP_U_BigVector: capacity overflow" );
		// dimhotepus: Cannot grow and cannot return an error from here,
		// failing fast is better than continuing with a full buffer.
		std::abort();
	}

	// dimhotepus: Compute new size once. Old code allocated 2 * ( memsize + 1 )
	// slots but stored memsize * 2 + 1, so the two disagreed and one slot was
	// wasted. Allocation and memsize are now guaranteed to match.
	const int new_size = memsize * 2 + 1;
	// dimhotepus: Cast to size_t before multiply to avoid int overflow.
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

		// dimhotepus: On failure realloc returns nullptr and the old block is
		// still valid, but we cannot report errors from here. Fail fast instead
		// of writing through a null pointer.
		if ( !new_elems )
		{
			IVP_ASSERT( !"IVP_U_BigVector: p_realloc failed" );
			std::abort();
		}
	}
	else
	{
		new_elems = (void **)p_malloc( new_bytes );

		// dimhotepus: One memcpy instead of per-element loop, faster, and
		// skipped when empty (memcpy with nullptr source is UB even for size 0).
		if ( n_elems > 0 )
		{
			std::memcpy( new_elems, elems, (size_t)n_elems * sizeof( void * ) );
		}

		// dimhotepus: Shared helper instead of open-coded this + 1 compare.
		if ( !is_inline_storage() )
		{
			p_free( elems );
		}
	}

	elems = new_elems;
	memsize = new_size;
}
