
#if defined(HK_HAVE_MSVC_INLINE_ASSEMBLY)

#include "winlite.h"

#include <intrin.h>

#elif defined(HK_HAVE_GNU_INLINE_ASSEMBLY)

#include <x86intrin.h>

#else
#	error HK_HAVE_QUERY_PERFORMANCE_TIMER is defined, but no implementation.
#endif

#include <algorithm>

inline hk_uint64 hk_query_cpu_timer_value(hk_uint32 &cpu_id)
{
	// See https://www.felixcloutier.com/x86/rdtscp
	// 
	// The RDTSCP instruction is not a serializing instruction, but it does wait
	// until all previous instructions have executed and all previous loads are
	// globally visible.  But it does not wait for previous stores to be
	// globally visible, and subsequent instructions may begin execution before
	// the read operation is performed.  The following items may guide software
	// seeking to order executions of RDTSCP:
	// * If software requires RDTSCP to be executed only after all previous
	// stores are globally visible, it can execute MFENCE immediately before
	// RDTSCP.
	// * If software requires RDTSCP to be executed prior to execution of any
	// subsequent instruction (including any memory accesses), it can execute
	// LFENCE immediately after RDTSCP.
	//
	// We do not mfence before as for timing only ordering matters, not finished
	// memory stores are ok. 
	const hk_uint64 tsc{__rdtscp(&cpu_id)};
	// Ensure no reordering aka acquire barrier.
	_mm_lfence();
	return tsc;
}

inline hk_uint64 hk_measure_cpu_timer_overhead()
{
	hk_uint32 coreIdStart, coreIdEnd;
	hk_uint64 overheads[64];
	for (size_t i{0}; i < std::size(overheads);)
	{
		hk_uint64 start = hk_query_cpu_timer_value(coreIdStart);
		hk_uint64 end = hk_query_cpu_timer_value(coreIdEnd);
		// Usually tsc synced over cores (constant_tsc), but due to UEFI / CPU
		// bugs all is possible.  So expect clock is monotonic only on a single
		// core.
		if (coreIdStart == coreIdEnd)
		{
			 overheads[i++] = end - start;
		}
	}

	// median.
	std::nth_element(std::begin(overheads),
		std::begin(overheads) + std::size(overheads) / 2,
		std::end(overheads));
	hk_uint64 median = overheads[std::size(overheads) / 2];

	return median;
}

inline hk_uint64 hk_query_cpu_current_frequency()
{
#ifdef _WIN32
	LARGE_INTEGER waitTime, startCount, curCount;
	hk_uint32 cpu_id_start, cpu_id_end;

	// Take 1/128 of a second for the measurement.
	QueryPerformanceFrequency( &waitTime );
	constexpr unsigned scale = 7;
	waitTime.QuadPart >>= scale;

	QueryPerformanceCounter( &startCount );
	const hk_uint64 startTicks = hk_query_cpu_timer_value(cpu_id_start);
	do
	{
		QueryPerformanceCounter( &curCount );
	}
	while ( curCount.QuadPart - startCount.QuadPart < waitTime.QuadPart );
	const hk_uint64 endTicks = hk_query_cpu_timer_value(cpu_id_end);

	if (cpu_id_start != cpu_id_end)
	{
		// dimhotepus: Thread migrated to another CPU core, results are
		// approximate.  Retry till thread executed on the same CPU core.
		return hk_query_cpu_current_frequency();
	}

	hk_uint64 freq = (endTicks - startTicks) << scale;
	if ( freq == 0 )
	{
		// Steam was seeing Divide-by-zero crashes on some Windows machines due to
		// WIN64_AMD_DUALCORE_TIMER_WORKAROUND that can cause rdtsc to effectively
		// stop.  Staging doesn't have the workaround but I'm checking in the fix
		// anyway.  Return a plausible speed and get on with our day.
		freq = 2000000000U;
	}
#else
	// assume 2000 Mhz for now
	freq = 2000000000U;
#endif
	return freq;
}

////////////////////////

class hk_Stopwatch_qpt
{
	public:

		hk_Stopwatch_qpt();

		void start();
		void stop();
		void reset();

		hk_real get_elapsed_time() const;
		hk_real get_split_time();

	private:

		hk_uint64	m_ticks_at_start;
		hk_uint64	m_ticks_at_split;
		hk_uint64	m_ticks_total;
		int			m_num_timings;
		hk_uint32	m_core_id;
		bool		m_running_flag;

		static hk_uint64	s_ticks_per_second;
		static hk_uint64	s_measure_overhead;
};

inline hk_Stopwatch_qpt::hk_Stopwatch_qpt()
{
	if(s_ticks_per_second==0)
		s_ticks_per_second = hk_query_cpu_current_frequency();
	if(s_measure_overhead==0)
		s_measure_overhead = hk_measure_cpu_timer_overhead();
	reset();
}

inline void hk_Stopwatch_qpt::start()
{
	HK_ASSERT(! m_running_flag);
	m_running_flag = true;
	m_ticks_at_start = hk_query_cpu_timer_value(m_core_id);
	m_ticks_at_split = m_ticks_at_start;
}

inline void hk_Stopwatch_qpt::stop()
{
	HK_ASSERT(m_running_flag);

	m_running_flag = false;
	hk_uint64 ticks_now = hk_query_cpu_timer_value(m_core_id);
	m_ticks_total += ticks_now - m_ticks_at_start;
	++m_num_timings;
}

inline void hk_Stopwatch_qpt::reset()
{
	m_ticks_at_start = 0;
	m_ticks_at_split = 0;
	m_ticks_total = 0;
	m_num_timings = 0;
	m_core_id = 0;
	m_running_flag = false;
}

inline hk_real hk_Stopwatch_qpt::get_elapsed_time() const
{
	return static_cast<hk_real>(m_ticks_total - s_measure_overhead) / static_cast<hk_real>(s_ticks_per_second);
}

inline hk_real hk_Stopwatch_qpt::get_split_time()
{
	hk_uint64 ticks_now = hk_query_cpu_timer_value(m_core_id);
	hk_uint64 sticks = ticks_now - m_ticks_at_split - s_measure_overhead;
	m_ticks_at_split = ticks_now;
	return static_cast<hk_real>(sticks) / static_cast<hk_real>(s_ticks_per_second);
}

