// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>

#include <ivu_types.hxx>

#include <atomic>
#include <cstdint>

// WIN32 ---------------------------------------------------------------------

namespace
{
// dimhotepus: Was a plain signed int. 'seed *= 75' overflowed it (UB), and
// concurrent ivp_rand() calls raced on it. uint32_t wraps by definition and gives
// the same bits as the old signed multiply did on x86, x86-64 and arm64.
// std::atomic keeps ivp_srand() visible to all threads (thread_local would not).
std::atomic< uint32_t > IVP_RAND_SEED{ 1 };

// dimhotepus: Lock-free is required, a mutex in a hot RNG would be a regression.
static_assert( std::atomic< uint32_t >::is_always_lock_free,
               "ivp_rand needs lock-free 32 bit atomics" );

// dimhotepus: 1 / 65536 is exact in float, so multiplying equals the old divide.
constexpr float rand_scale = 1.0f / 65536.0f;
} // namespace

// returns 0 .. 1.0 (exclusive)
IVP_FLOAT ivp_rand()
{
	// dimhotepus: Atomic read-modify-write so two threads never get the same
	// value or lose an update. Relaxed order: no other data depends on it.
	uint32_t old = IVP_RAND_SEED.load( std::memory_order_relaxed );
	uint32_t next;
	do
	{
		next = old * 75u;
	} while ( !IVP_RAND_SEED.compare_exchange_weak( old, next, std::memory_order_relaxed ) );

	return IVP_FLOAT( ( next & 0xffffu ) * rand_scale );
}

void ivp_srand( int seed )
{
	// dimhotepus: Conversion int -> uint32_t is modular and well defined.
	uint32_t useed = static_cast< uint32_t >( seed );

	// dimhotepus: Only the low 16 bits reach the result and they depend only on the
	// low 16 bits of the seed, so any multiple of 65536 (not just 0) made
	// ivp_rand() return 0.0 forever.
	if ( ( useed & 0xffffu ) == 0 )
		useed = 1;

	IVP_RAND_SEED.store( useed, std::memory_order_relaxed );
}

int ivp_srand_read()
{
	// dimhotepus: Explicit cast, uint32_t -> int is implementation-defined before
	// C++20 but two's complement on every supported target.
	return static_cast< int >( IVP_RAND_SEED.load( std::memory_order_relaxed ) );
}
