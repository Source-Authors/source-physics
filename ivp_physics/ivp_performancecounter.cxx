// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>

#ifndef WIN32
#pragma implementation "ivp_performancecounter.hxx"
#endif
#include <ivp_performancecounter.hxx>

#include <chrono>
#include <cstring>

namespace
{

constexpr inline double kScaleFactorForReducingPrecisionLoss = 1e6;
constexpr inline double  kInvertedScaleFactorForReducingPrecisionLoss =
  1.0 / kScaleFactorForReducingPrecisionLoss;

}  // namespace

IVP_PerformanceCounter_Simple::IVP_PerformanceCounter_Simple() :
	ref_time( std::chrono::steady_clock::now() ),
	count_PSIs( 0 ),
	counting( IVP_PE_PSI_START ),
	time_of_last_reset( 0 )
{
	std::memset( counter, 0, sizeof( counter ) );
}

void IVP_PerformanceCounter_Simple::pcount( IVP_PERFORMANCE_ELEMENT el )
{
	if ( el == IVP_PE_PSI_UNIVERSE )
		count_PSIs++;

	const auto   now = std::chrono::steady_clock::now();
	const double elapsed_seconds =
		std::chrono::duration< double >( now - ref_time ).count();

	ref_time = now;
	counter[counting] +=
		kScaleFactorForReducingPrecisionLoss * elapsed_seconds;
	counting = el;
}

void IVP_PerformanceCounter_Simple::start_pcount()
{
	counting = IVP_PE_PSI_START;
}

void IVP_PerformanceCounter_Simple::stop_pcount() {}

void IVP_PerformanceCounter_Simple::reset_and_print_performance_counters(
IVP_Time current_time )
{
	const IVP_DOUBLE universe_samples_count = count_PSIs;
	if ( universe_samples_count == 0.0 )
		return;

	const IVP_DOUBLE collision =
		counter[IVP_PE_PSI_UNIVERSE] +
		counter[IVP_PE_PSI_SHORT_MINDISTS] +
		counter[IVP_PE_PSI_CRITICAL_MINDISTS] +
		counter[IVP_PE_PSI_HULL] +
		counter[IVP_PE_AT_INIT];

	const IVP_DOUBLE dynamics =
		counter[IVP_PE_PSI_CONTROLLERS] +
		counter[IVP_PE_PSI_INTEGRATORS];

	const IVP_DOUBLE sum = collision + dynamics;
	const IVP_DOUBLE factor =
		kInvertedScaleFactorForReducingPrecisionLoss / universe_samples_count;

	ivp_message(
		"[performance] TOTAL %2.1f%% %2.2fs\nCOLLISION %2.2fs\n"
		"DYNAMIC %2.2fs\n\nUNIVERSE: %2.2fs\nCONTROLLERS: %2.2fs\n"
		"INTEGRATORS: %2.2fs\nHULL: %2.2fs\nSHORT MINDISTS: %2.2fs\n"
		"CRITICAL MINDISTS: %2.2fs\nSIMULATE %2.2fs\n",
		sum * factor * 100.0, sum * factor,
		collision * factor, dynamics * factor,
		counter[IVP_PE_PSI_UNIVERSE] * factor,
		counter[IVP_PE_PSI_CONTROLLERS] * factor,
		counter[IVP_PE_PSI_INTEGRATORS] * factor,
		counter[IVP_PE_PSI_HULL] * factor,
		counter[IVP_PE_PSI_SHORT_MINDISTS] * factor,
		counter[IVP_PE_PSI_CRITICAL_MINDISTS] * factor,
		counter[IVP_PE_AT_INIT] * factor );

	counting = IVP_PE_PSI_START;
	count_PSIs = 0;
	std::memset( counter, 0, sizeof( counter ) );
	time_of_last_reset = current_time;
	ref_time = std::chrono::steady_clock::now();
}

void IVP_PerformanceCounter_Simple::environment_is_going_to_be_deleted(
IVP_Environment * )
{
	P_DELETE_THIS( this );
}

IVP_PerformanceCounter_Simple::~IVP_PerformanceCounter_Simple() {}
