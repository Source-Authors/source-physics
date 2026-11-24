#ifdef WIN32
#include <cfloat>
#endif

#include <cmath>  // std::sqrt
#include <cstdint>
#include <cstring>

// dimhotepus: Old code type-punned through pointer casts and unions with
// bitfields. That is UB (strict aliasing / inactive union member), and bitfield
// order is implementation-defined, so it needed WIN32/PSXII/LINUX vs. other
// branches and was wrong on little-endian targets matching none of them (macOS,
// arm64). memcpy into an integer is defined behavior, endian-neutral for the
// exponent extraction, and compiles to a single register move.
namespace ivp_fast_math_detail
{
[[nodiscard]] inline uint32_t float_bits( float a )
{
	uint32_t bits;
	std::memcpy( &bits, &a, sizeof( bits ) );
	return bits;
}

[[nodiscard]] inline uint64_t double_bits( double a )
{
	uint64_t bits;
	std::memcpy( &bits, &a, sizeof( bits ) );
	return bits;
}

[[nodiscard]] inline double double_from_bits( uint64_t bits )
{
	double a;
	std::memcpy( &a, &bits, sizeof( a ) );
	return a;
}

// dimhotepus: One typed constant per format instead of macros with
// inconsistent names (IVP_EXP_FOR_ONE vs P_EXP_FOR_ONE) that could collide.
constexpr int float_exp_for_one = 0x7f;
constexpr int double_exp_for_one = 0x3ff;
} // namespace ivp_fast_math_detail

// dimhotepus: Keep the old macro for source compatibility with callers.
#ifndef IVP_EXP_FOR_ONE
#if defined( IVP_NO_DOUBLE ) && !defined( SUN )
#define IVP_EXP_FOR_ONE (ivp_fast_math_detail::float_exp_for_one)
#else
#define IVP_EXP_FOR_ONE (ivp_fast_math_detail::double_exp_for_one)
#endif
#endif

#if defined( IVP_NO_DOUBLE ) && !defined( SUN )
// dimhotepus: Unbiased binary exponent of a float. Mask drops the sign bit,
// same result as the old 8-bit bitfield. Removed stray ';' after function body
// (-Wextra-semi).
[[nodiscard]] inline int PFM_LD( float a )
{
	return (int)( ( ivp_fast_math_detail::float_bits( a ) >> 23 ) & 0xffu ) -
	ivp_fast_math_detail::float_exp_for_one;
}
#else
// dimhotepus: One definition for all platforms. The old code had two
// p_double_ieee unions (POSIX/WIN32 and SUN/PPC/GEKKO) that collided when both
// macro sets were defined, and PPC/GEKKO big-endian layout is no longer special.
[[nodiscard]] inline int PFM_LD( double a )
{
	return (int)( ( ivp_fast_math_detail::double_bits( a ) >> 52 ) & 0x7ffu ) -
	ivp_fast_math_detail::double_exp_for_one;
}
#endif

class IVP_Fast_Math
{
public:
#if defined( IVP_NO_DOUBLE )
	static IVP_DOUBLE isqrt( IVP_DOUBLE square, int /*resolution_steps*/ )
	{
		return 1.0f / IVP_Inline_Math::ivp_sqrtf( square );
	}

	static IVP_DOUBLE sqrt( IVP_DOUBLE x ) { return IVP_Inline_Math::ivp_sqrtf( x ); }

#else
	// fast 1/sqrt(x),
	// resolution for resolution_steps
	// 0 -> 1e-3
	// 1 -> 1e-7
	// 2 -> 1e-14
	// 3 -> 1e-16
	[[nodiscard]] static double isqrt( double square, int resolution_steps )
	{
		IVP_ASSERT( IVP_Inline_Math::fabsd( square ) > 0.0 );

		// dimhotepus: Integer bit trick on the high 32 bits, no aliasing and no
		// dependence on which union half is "high" per endianness. Low word of
		// the initial guess is 0 (it was h.val = 1.0, low word 0), so the guess
		// is built directly from the high word. Unsigned math: for positive input
		// hi < 0x7ff00000, so the subtraction cannot wrap.
		const uint32_t hi = (uint32_t)( ivp_fast_math_detail::double_bits( square ) >> 32 );
		const uint32_t guess_hi = ( ( 0x7ff00000u - hi ) >> 1 ) + 0x1ff00000u;

		const double squareh = square * 0.5;
		double       inv_sqrt = ivp_fast_math_detail::double_from_bits( (uint64_t)guess_hi << 32 );

		// dimhotepus: 0.5 instead of 0.5f, same value, but no float literal in
		// double math (-Wdouble-promotion style warnings).
		inv_sqrt += inv_sqrt * ( 0.5 - inv_sqrt * inv_sqrt * squareh );
		inv_sqrt += inv_sqrt * ( 0.5 - inv_sqrt * inv_sqrt * squareh );
		if ( resolution_steps > 0 )
			inv_sqrt += inv_sqrt * ( 0.5 - ( inv_sqrt * inv_sqrt * squareh ) );
		if ( resolution_steps > 1 )
			inv_sqrt += inv_sqrt * ( 0.5 - ( inv_sqrt * inv_sqrt * squareh ) );
		if ( resolution_steps > 2 )
			inv_sqrt += inv_sqrt * ( 0.5 - ( inv_sqrt * inv_sqrt * squareh ) );

		// dimhotepus: 1.0 / 0.001 as double, 0.001f is not a valid double epsilon.
		IVP_ASSERT( IVP_Inline_Math::fabsd( 1.0 - inv_sqrt * inv_sqrt * square ) < 0.001 );
		return inv_sqrt;
	}

	// dimhotepus: std::sqrt instead of ::sqrt, <cmath> guarantees the std:: name
	// (the global one is optional) and picks the right overload.
	static IVP_DOUBLE sqrt( IVP_DOUBLE x ) { return std::sqrt( x ); }
#endif
};
