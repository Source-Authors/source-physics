// dimhotepus: <memory.h> clashes with this project's own memory.h when the
// include path contains hk_base/memory, and is non-standard. Use <cstring>
// on all platforms (also makes the HK_PS2 branch unnecessary).
#include <cstring>

inline void *hk_Memory::memcpy( void *dest, const void *src, hk_size_t size )
{
	return std::memcpy( dest, src, size );
}

inline void *hk_Memory::memset( void *dest, hk_uchar val, hk_size_t size )
{
	return std::memset( dest, static_cast< int >( val ), size );
}
