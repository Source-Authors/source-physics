// Copyright (C) Ipion Software GmbH 1999-2000. All rights reserved.

#include <ivp_physics.hxx>

#ifndef WIN32
#pragma implementation "ivp_performancecounter.hxx"
#endif
#include <ivp_performancecounter.hxx>

namespace {

constexpr inline double kScaleFactorForReducingPrecisionLoss = 1e6;
constexpr inline double kInvertedScaleFactorForReducingPrecisionLoss =
    1.0 / kScaleFactorForReducingPrecisionLoss;

}  // namespace

#if defined(WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

namespace {

[[nodiscard]] double GetInvertedPerformanceFrequency() {
  // address of current frequency
  LARGE_INTEGER frequency;
  // Since XP+ always succeeds.
  (void)QueryPerformanceFrequency(&frequency);

  return 1.0 / max(frequency.QuadPart, 1);
}

[[nodiscard]] long long GetPerformanceCounter() {
  LARGE_INTEGER profile_counter;
  // Since XP+ always succeeds.
  (void)QueryPerformanceCounter(&profile_counter);

  return profile_counter.QuadPart;
}

}  // namespace

void IVP_PerformanceCounter_Simple::reset_and_print_performance_counters(
    IVP_Time current_time) {
  const IVP_DOUBLE universe_samples_count = count_PSIs;
  if (universe_samples_count == 0.0) return;

  IVP_DOUBLE collision = counter[IVP_PE_PSI_UNIVERSE] +
                         counter[IVP_PE_PSI_SHORT_MINDISTS] +
                         counter[IVP_PE_PSI_CRITICAL_MINDISTS] +
                         counter[IVP_PE_PSI_HULL] + counter[IVP_PE_AT_INIT];
  IVP_DOUBLE dynamics =
      counter[IVP_PE_PSI_CONTROLLERS] + counter[IVP_PE_PSI_INTEGRATORS];
  IVP_DOUBLE sum = collision + dynamics;

  const IVP_DOUBLE factor =
      kInvertedScaleFactorForReducingPrecisionLoss / universe_samples_count;

  ivp_message(
      "[performance] TOTAL %2.1f%% %2.2fs\nCOLLISION %2.2fs\nDYNAMIC %2.2fs\n\n"
      "UNIVERSE: %2.2fs\nCONTROLLERS: %2.2fs\nINTEGRATORS: %2.2fs\n"
      "HULL: %2.2fs\nSHORT MINDISTS: %2.2fs\n"
      "CRITICAL MINDISTS: %2.2fs\nSIMULATE %2.2fs\n",
      sum * factor * 100.0, sum * factor, collision * factor, dynamics * factor,
      counter[IVP_PE_PSI_UNIVERSE] * factor,
      counter[IVP_PE_PSI_CONTROLLERS] * factor,
      counter[IVP_PE_PSI_INTEGRATORS] * factor,
      counter[IVP_PE_PSI_HULL] * factor,
      counter[IVP_PE_PSI_SHORT_MINDISTS] * factor,
      counter[IVP_PE_PSI_CRITICAL_MINDISTS] * factor,
      counter[IVP_PE_AT_INIT] * factor);

#ifdef WIN32
  ref_counter64 = GetPerformanceCounter();
#endif

  counting = IVP_PE_PSI_START;
  count_PSIs = 0;

  memset(counter, 0, sizeof(counter));

  time_of_last_reset = current_time;
}

void IVP_PerformanceCounter_Simple::environment_is_going_to_be_deleted(
    IVP_Environment *) {
  P_DELETE_THIS(this);
}

IVP_PerformanceCounter_Simple::~IVP_PerformanceCounter_Simple() {}

#if !defined(WIN32)
IVP_PerformanceCounter_Simple::IVP_PerformanceCounter_Simple() {
  counting = IVP_PE_PSI_START;
  count_PSIs = 0;

  memset(counter, 0, sizeof(counter));
}
#endif

IVP_PerformanceCounter_Simple::IVP_PerformanceCounter_Simple()
    : inv_counter_freq{GetInvertedPerformanceFrequency()},
      ref_counter64{GetPerformanceCounter()} {
  counting = IVP_PE_PSI_START;
  count_PSIs = 0;

  memset(counter, 0, sizeof(counter));
  time_of_last_reset = 0;
}

void IVP_PerformanceCounter_Simple::pcount(IVP_PERFORMANCE_ELEMENT el) {
  if (el == IVP_PE_PSI_UNIVERSE) count_PSIs++;

  const long long now{GetPerformanceCounter()};
  const long long diff0{now - ref_counter64};

  ref_counter64 = now;

  counter[counting] +=
      kScaleFactorForReducingPrecisionLoss * double(diff0) * inv_counter_freq;
  counting = el;
}

void IVP_PerformanceCounter_Simple::start_pcount() {
  counting = IVP_PE_PSI_START;
}

void IVP_PerformanceCounter_Simple::stop_pcount() {}

#else
void IVP_PerformanceCounter_Simple::pcount(IVP_PERFORMANCE_ELEMENT) {}
void IVP_PerformanceCounter_Simple::start_pcount() {}
void IVP_PerformanceCounter_Simple::stop_pcount() {}
#endif
