#include "hk_base/base.h"
#include "hk_base/memory/memory_util.h"

#include <cstddef>
#include <mutex>

void hk_Memory_Util::print_statistics( hk_Memory *mem, hk_Console *out )
{
	constexpr unsigned k_classes =
		static_cast< unsigned >( hk_MEMORY_CLASS::HK_MEMORY_CLASS_MAX );

	const char *enum_to_string[k_classes] = { nullptr };
	{
#define H( a ) enum_to_string[static_cast< unsigned >( a )] = #a
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_UNKNOWN );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_DUMMY );

		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_ARRAY );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_HASH );

		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_SORTED_SET );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_DENSE_VECTOR );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_ENTITY );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_RIGID_BODY );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_EFFECTOR );

		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_CORE );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_CONSTRAINT );

		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_MANAGER );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_SIMUNINT );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_SIM_SLOT );

		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_PSI_TIME );

		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_BROAD_PHASE );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_CA_DISPATCHER );

		// havok compat
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_GENERIC );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_STL );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_MATH );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_EVENT );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_ACTION );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_GEOMETRY );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_CDINFO );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_DISPLAY );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_EXPORT );

		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_USR1 );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_USR2 );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_USR3 );
		H( hk_MEMORY_CLASS::HK_MEMORY_CLASS_USR4 );
#undef H
	}

	// dimhotepus: Take a consistent snapshot under the allocator lock, then
	// print without it. out->printf may allocate through hk_Memory, which
	// would deadlock on a non-recursive mutex.
	hk_Memory::hk_Memory_Statistics stats[k_classes];
	hk_size_t                       blocks_in_use[HK_MEMORY_MAX_ROW];
	hk_size_t                       free_blocks[HK_MEMORY_MAX_ROW];
	hk_uint16                       row_to_size[HK_MEMORY_MAX_ROW];
	{
		std::lock_guard< std::mutex > lock( mem->m_mutex );

		for ( unsigned j = 0; j < k_classes; j++ )
			stats[j] = mem->m_statistics[j];

		for ( int j = 0; j < HK_MEMORY_MAX_ROW; j++ )
		{
			hk_size_t n = 0;
			for ( hk_Memory::hk_Memory_Elem *el = mem->m_free_list[j]; el;
			      el = el->m_next )
			{
				++n;
			}
			free_blocks[j] = n;
			blocks_in_use[j] = mem->m_blocks_in_use[j];
			row_to_size[j] = mem->m_row_to_size[j];
		}
	}

	hk_size_t max_mem = 0, current_mem = 0;
	{
		// summary
		// dimhotepus: Old loop did 'continue' for DUMMY without incrementing
		// the counter, so it never advanced and summed only the first
		// classes. Iterate by index.
		for ( unsigned j = 0; j < k_classes; j++ )
		{
			if ( j == static_cast< unsigned >( hk_MEMORY_CLASS::HK_MEMORY_CLASS_DUMMY ) )
				continue;

			max_mem += stats[j].m_max_size_in_use;
			current_mem += stats[j].m_size_in_use;
		}
	}

	out->printf( "\nMemory Summary\n" );
	out->printf( "**************\n" );

	out->printf( "\n%32s: Bytes allocated %zu, max bytes allocated %zu\n",
	             "SUMMARY", static_cast< size_t >( current_mem ),
	             static_cast< size_t >( max_mem ) );

	{
		// details per type
		out->printf( "\nDetails per type\n" );
		out->printf( "****************\n" );
		for ( unsigned j = 0; j < k_classes; j++ )
		{
			if ( j == static_cast< unsigned >( hk_MEMORY_CLASS::HK_MEMORY_CLASS_DUMMY ) )
				continue;

			const hk_Memory::hk_Memory_Statistics &s = stats[j];
			if ( !s.m_max_size_in_use )
			{
				continue;
			}

			const char *type_name = enum_to_string[j];
			if ( !type_name )
			{
				type_name = "hk_Memory::print_statistics does not know type";
			}
			// dimhotepus: avg_size had spurious '+ 1'.
			out->printf(
			"%32s: blocks: %4zu  size: %5zu  max_size %5zu  avg_size %5zu  "
			"allocs %6zu\n",
			type_name, static_cast< size_t >( s.m_blocks_in_use ),
			static_cast< size_t >( s.m_size_in_use ),
			static_cast< size_t >( s.m_max_size_in_use ),
			static_cast< size_t >( s.m_blocks_in_use ? s.m_size_in_use / s.m_blocks_in_use
			                                         : 0 ),
			static_cast< size_t >( s.m_n_allocates ) );
		}
	}

	{
		// details per size
		out->printf( "\nDetails per size\n" );
		out->printf( "****************\n" );

		for ( int j = 0; j < HK_MEMORY_MAX_ROW; j++ )
		{
			out->printf( "%32s  blocks %4zu  size %5u  free_elems %4zu  total %6zu\n",
			             "", static_cast< size_t >( blocks_in_use[j] ),
			             static_cast< unsigned >( row_to_size[j] ),
			             static_cast< size_t >( free_blocks[j] ),
			             static_cast< size_t >( blocks_in_use[j] * row_to_size[j] ) );
		}
	}
}