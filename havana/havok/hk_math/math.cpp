#include <hk_math/vecmath.h>

// BSD rand function
unsigned int hk_Math::hk_random_seed = 'h'+'a'+'v'+'o'+'k';

void hk_Math::srand01( unsigned seedVal )
{
	hk_random_seed = seedVal;
}

hk_real hk_Math::rand01()
{
	constexpr unsigned a = 1103515245;
	constexpr unsigned c = 12345;
	constexpr unsigned m = UINT_MAX >> 1;
	hk_random_seed = (a * hk_random_seed + c ) & m;
	return static_cast<hk_real>(hk_random_seed) / m;
}
