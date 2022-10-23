// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/standalone-test/standalone.h>
#include <lib/zx/clock.h>
#include <lib/zx/job.h>
#include <lib/zx/profile.h>
#include <lib/zx/resource.h>
#include <lib/zx/result.h>
#include <lib/zx/thread.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/syscalls/profile.h>
#include <zircon/syscalls/resource.h>
#include <zircon/syscalls/system.h>
#include <zircon/types.h>

#include <iterator>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

#include <zxtest/zxtest.h>

namespace {

constexpr uint32_t kInvalidTopic = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kInvalidCpu = std::numeric_limits<uint32_t>::max();
constexpr size_t kInvalidInfoCount = std::numeric_limits<size_t>::max();

constexpr zx_sched_deadline_params_t kTestThreadDeadlineParams = {
    .capacity = ZX_MSEC(5), .relative_deadline = ZX_MSEC(20), .period = ZX_MSEC(20)};

constexpr uint32_t kTestThreadCpu = 1;
static_assert(kTestThreadCpu < ZX_CPU_SET_MAX_CPUS);

constexpr zx_cpu_set_t CpuNumToCpuSet(size_t cpu_num) {
  zx_cpu_set_t cpu_set{};
  cpu_set.mask[cpu_num / ZX_CPU_SET_BITS_PER_WORD] = 1 << (cpu_num % ZX_CPU_SET_BITS_PER_WORD);
  return cpu_set;
}

zx::result<zx_info_thread_stats_t> GetThreadStats(const zx::thread& thread) {
  zx_info_thread_stats_t info;
  const zx_status_t status =
      thread.get_info(ZX_INFO_THREAD_STATS, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(info);
}

zx::result<size_t> GetCpuCount() {
  size_t actual, available;
  const zx_status_t status =
      standalone::GetRootResource()->get_info(ZX_INFO_CPU_STATS, nullptr, 0, &actual, &available);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(available);
}

zx::result<zx::resource> GetSystemCpuResource() {
  zx::resource system_cpu_resource;
  const zx_status_t status =
      zx::resource::create(*standalone::GetSystemRootResource(), ZX_RSRC_KIND_SYSTEM,
                           ZX_RSRC_SYSTEM_CPU_BASE, 1, nullptr, 0, &system_cpu_resource);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(system_cpu_resource));
}

zx::result<zx::resource> GetSystemInfoResource() {
  zx::resource system_info_resource;
  const zx_status_t status =
      zx::resource::create(*standalone::GetSystemRootResource(), ZX_RSRC_KIND_SYSTEM,
                           ZX_RSRC_SYSTEM_INFO_BASE, 1, nullptr, 0, &system_info_resource);
  if (status != ZX_OK) {
    return zx::error(status);
  }
  return zx::ok(std::move(system_info_resource));
}

zx::result<zx::unowned_resource> GetMmioResource() {
  return zx::ok(standalone::GetMmioRootResource());
}

template <typename Callable>
zx_status_t RunThread(Callable&& callable) {
  zx_profile_info_t info = {};
  info.flags = ZX_PROFILE_INFO_FLAG_DEADLINE | ZX_PROFILE_INFO_FLAG_CPU_MASK;
  info.deadline_params = kTestThreadDeadlineParams;
  info.cpu_affinity_mask = CpuNumToCpuSet(kTestThreadCpu);

  zx::unowned_job root_job(zx::job::default_job());
  if (!root_job->is_valid()) {
    return ZX_ERR_INVALID_ARGS;
  }

  zx::profile profile;
  zx_status_t result = zx::profile::create(*root_job, 0u, &info, &profile);
  if (result != ZX_OK) {
    return result;
  }

  std::thread worker([&callable, &result, &profile]() {
    result = zx::thread::self()->set_profile(profile, 0);
    if (result != ZX_OK) {
      return;
    }

    std::forward<Callable>(callable)();

    result = ZX_OK;
  });
  worker.join();

  return result;
}

}  // anonymous namespace

TEST(SystemCpu, SetPerformanceInfoArgumentValidation) {
  const zx::result resource = GetSystemCpuResource();
  ASSERT_TRUE(resource.is_ok());

  // Test invalid handle -> ZX_ERR_BAD_HANDLE.
  {
    zx_cpu_performance_info_t info[] = {{0, {1, 0}}};
    const zx_status_t status =
        zx_system_set_performance_info(ZX_HANDLE_INVALID, kInvalidTopic, &info, std::size(info));
    EXPECT_EQ(ZX_ERR_BAD_HANDLE, status);
  }

  // Test incorrect resource kind -> ZX_ERR_WRONG_TYPE.
  {
    const zx::result info_resource = GetMmioResource();
    ASSERT_TRUE(info_resource.is_ok());

    zx_cpu_performance_info_t info[] = {{0, {1, 0}}};
    const zx_status_t status =
        zx_system_set_performance_info(info_resource->get(), kInvalidTopic, &info, std::size(info));
    EXPECT_EQ(ZX_ERR_WRONG_TYPE, status);
  }

  // Test incorrect system resource range -> ZX_ERR_OUT_OF_RANGE.
  {
    const zx::result info_resource = GetSystemInfoResource();
    ASSERT_TRUE(info_resource.is_ok());

    zx_cpu_performance_info_t info[] = {{0, {1, 0}}};
    const zx_status_t status =
        zx_system_set_performance_info(info_resource->get(), kInvalidTopic, &info, std::size(info));
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
  }

  // Test invalid topic -> ZX_ERR_INVALID_ARGS.
  {
    zx_cpu_performance_info_t info[] = {{0, {1, 0}}};
    const zx_status_t status =
        zx_system_set_performance_info(resource->get(), kInvalidTopic, &info, std::size(info));
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  }

  // Test invalid info -> ZX_ERR_INVALID_ARGS.
  {
    zx_cpu_performance_info_t info[] = {{0, {1, 0}}};
    const zx_status_t status = zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE,
                                                              nullptr, std::size(info));
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  }

  // Test count == 0 -> ZX_ERR_OUT_OF_RANGE.
  {
    zx_cpu_performance_info_t info[] = {{0, {1, 0}}};
    const zx_status_t status =
        zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE, &info, 0);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
  }

  // Test info_count > num_cpus -> ZX_ERR_OUT_OF_RANGE.
  {
    zx_cpu_performance_info_t info[] = {{0, {1, 0}}};
    const zx_status_t status = zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE,
                                                              &info, kInvalidInfoCount);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
  }

  // Test invalid CPU number -> ZX_ERR_OUT_OF_RANGE.
  {
    zx_cpu_performance_info_t info[] = {{kInvalidCpu, {1, 0}}};
    const zx_status_t status =
        zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE, &info, std::size(info));
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
  }

  // Test invalid perf scale -> ZX_ERR_OUT_OF_RANGE.
  {
    zx_cpu_performance_info_t info[] = {{0, {0, 0}}};
    const zx_status_t status =
        zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE, &info, std::size(info));
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
  }

  // Test invalid sort order -> ZX_ERR_INVALID_ARGS.
  if (const zx::result cpu_count = GetCpuCount(); cpu_count.is_ok() && *cpu_count >= 2) {
    zx_cpu_performance_info_t info[] = {{0, {1, 0}}, {0, {1, 0}}};
    const zx_status_t status =
        zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE, &info, std::size(info));
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  }
}

TEST(SystemCpu, GetPerformanceInfoArgumentValidation) {
  const zx::result resource = GetSystemCpuResource();
  ASSERT_TRUE(resource.is_ok());

  const zx::result cpu_count = GetCpuCount();
  ASSERT_TRUE(cpu_count.is_ok());
  std::vector<zx_cpu_performance_info_t> info(*cpu_count);

  // Test invalid handle -> ZX_ERR_BAD_HANDLE.
  {
    size_t count;
    const zx_status_t status = zx_system_get_performance_info(ZX_HANDLE_INVALID, kInvalidTopic,
                                                              info.size(), info.data(), &count);
    EXPECT_EQ(ZX_ERR_BAD_HANDLE, status);
  }

  // Test incorrect resource kind -> ZX_ERR_WRONG_TYPE.
  {
    const zx::result info_resource = GetMmioResource();
    ASSERT_TRUE(info_resource.is_ok());

    size_t count;
    const zx_status_t status = zx_system_get_performance_info(info_resource->get(), kInvalidTopic,
                                                              info.size(), info.data(), &count);
    EXPECT_EQ(ZX_ERR_WRONG_TYPE, status);
  }

  // Test incorrect system resource range -> ZX_ERR_OUT_OF_RANGE.
  {
    const zx::result info_resource = GetSystemInfoResource();
    ASSERT_TRUE(info_resource.is_ok());

    size_t count;
    const zx_status_t status = zx_system_get_performance_info(info_resource->get(), kInvalidTopic,
                                                              info.size(), info.data(), &count);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
  }

  // Test invalid topic -> ZX_ERR_INVALID_ARGS.
  {
    size_t count;
    const zx_status_t status = zx_system_get_performance_info(resource->get(), kInvalidTopic,
                                                              info.size(), info.data(), &count);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  }

  // Test info_count == 0 -> ZX_ERR_OUT_OF_RANGE.
  {
    size_t count;
    const zx_status_t status =
        zx_system_get_performance_info(resource->get(), ZX_CPU_PERF_SCALE, 0, info.data(), &count);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
  }

  // Test info_count > num_cpus -> ZX_ERR_OUT_OF_RANGE.
  {
    size_t count;
    const zx_status_t status = zx_system_get_performance_info(resource->get(), ZX_CPU_PERF_SCALE,
                                                              info.size() + 1, info.data(), &count);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
  }

  // Test info_count < num_cpus -> ZX_ERR_OUT_OF_RANGE.
  {
    size_t count;
    const zx_status_t status = zx_system_get_performance_info(resource->get(), ZX_CPU_PERF_SCALE,
                                                              info.size() - 1, info.data(), &count);
    EXPECT_EQ(ZX_ERR_OUT_OF_RANGE, status);
  }

  // Test invalid output_count -> ZX_ERR_INVALID_ARGS.
  {
    const zx_status_t status = zx_system_get_performance_info(resource->get(), ZX_CPU_PERF_SCALE,
                                                              info.size(), info.data(), nullptr);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, status);
  }
}

TEST(SystemCpu, GetPerformanceInfo) {
  const zx::result resource = GetSystemCpuResource();
  ASSERT_TRUE(resource.is_ok());

  const zx::result cpu_count = GetCpuCount();
  ASSERT_TRUE(cpu_count.is_ok());

  std::vector<zx_cpu_performance_info_t> info(*cpu_count);
  std::vector<zx_cpu_performance_info_t> default_info(*cpu_count);

  {
    size_t count = kInvalidInfoCount;
    const zx_status_t status =
        zx_system_get_performance_info(resource->get(), ZX_CPU_DEFAULT_PERF_SCALE,
                                       default_info.size(), default_info.data(), &count);
    ASSERT_OK(status);
    ASSERT_EQ(default_info.size(), count);

    uint32_t last_cpu = kInvalidCpu;
    for (const auto& entry : default_info) {
      EXPECT_TRUE(last_cpu == kInvalidCpu || entry.logical_cpu_number > last_cpu);
      last_cpu = entry.logical_cpu_number;
      EXPECT_FALSE(entry.performance_scale.integral_part == 0 &&
                   entry.performance_scale.fractional_part == 0);
    }
  }

  {
    size_t count = kInvalidInfoCount;
    const zx_status_t status = zx_system_get_performance_info(
        resource->get(), ZX_CPU_PERF_SCALE, default_info.size(), default_info.data(), &count);
    ASSERT_OK(status);
    ASSERT_EQ(default_info.size(), count);

    uint32_t last_cpu = kInvalidCpu;
    for (const auto& entry : default_info) {
      EXPECT_TRUE(last_cpu == kInvalidCpu || entry.logical_cpu_number > last_cpu);
      last_cpu = entry.logical_cpu_number;
      EXPECT_FALSE(entry.performance_scale.integral_part == 0 &&
                   entry.performance_scale.fractional_part == 0);
    }
  }
}

// Verify that the scheduler's target preemption time is properly updated when the performance scale
// changes. Failure to maintain consistency will result in a kernel panic. See fxbug.dev/86901.
TEST(SystemCpu, TargetPreemptionTimeAssert) {
  if (GetCpuCount() < kTestThreadCpu + 1) {
    return;
  }

  const zx::result resource = GetSystemCpuResource();
  ASSERT_TRUE(resource.is_ok());

  const zx::result cpu_count = GetCpuCount();
  ASSERT_TRUE(cpu_count.is_ok());
  std::vector<zx_cpu_performance_info_t> original_performance_info(*cpu_count);

  size_t count = 0;
  ASSERT_OK(zx_system_get_performance_info(resource->get(), ZX_CPU_PERF_SCALE,
                                           original_performance_info.size(),
                                           original_performance_info.data(), &count));
  EXPECT_EQ(count, original_performance_info.size());

  const auto spin = [](const zx::duration spin_duration) {
    const zx::time time_end = zx::clock::get_monotonic() + spin_duration;
    zx::time now;
    do {
      now = zx::clock::get_monotonic();
    } while (now < time_end);
  };

  ASSERT_OK(RunThread([&] {
    for (int i = 0; i < 10; i++) {
      // Set the perf scale to 1.0 for the start of the period.
      zx_cpu_performance_info_t perf_scale_one[] = {{kTestThreadCpu, {1, 0}}};
      ASSERT_OK(zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE, &perf_scale_one,
                                               std::size(perf_scale_one)));

      // Yield to start a new period.
      zx::nanosleep(zx::time{0});

      // Spin until half capacity is exhausted.
      spin(zx::duration{kTestThreadDeadlineParams.capacity / 2});

      // Set the perf scale to 0.5 for the remainder of the period.
      zx_cpu_performance_info_t perf_scale_half[] = {{kTestThreadCpu, {0, 1u << 31}}};
      ASSERT_OK(zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE, &perf_scale_half,
                                               std::size(perf_scale_half)));

      // Spin until after the scaled capacity is exhausted: C / 2 / 0.5 = C
      spin(zx::duration{kTestThreadDeadlineParams.capacity} + zx::usec(100));
    }
  }));

  ASSERT_OK(zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE,
                                           original_performance_info.data(),
                                           original_performance_info.size()));
}

TEST(SystemCpu, ScaleBandwidth) {
  // TODO(fxbug.dev/85846): Disabled while flakeds are investigated.
  return;

  if (GetCpuCount() < kTestThreadCpu + 1) {
    return;
  }

  const zx_status_t run_thread_result = RunThread([&] {
    const zx::result resource = GetSystemCpuResource();
    ASSERT_TRUE(resource.is_ok());

    const zx::result cpu_count = GetCpuCount();
    ASSERT_TRUE(cpu_count.is_ok());
    std::vector<zx_cpu_performance_info_t> original_info(*cpu_count);

    size_t count = kInvalidInfoCount;
    zx_status_t result = zx_system_get_performance_info(
        resource->get(), ZX_CPU_PERF_SCALE, original_info.size(), original_info.data(), &count);

    const zx_cpu_performance_scale_t scale_one_half{0, 1u << 31};
    zx_cpu_performance_info_t info[] = {{kTestThreadCpu, scale_one_half}};
    result =
        zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE, &info, std::size(info));
    ASSERT_OK(result);

    // Sleep for at least one period to guarantee starting in a new period.
    zx::nanosleep(zx::deadline_after(zx::duration(kTestThreadDeadlineParams.period)));

    const zx::result<zx_info_thread_stats_t> stats_begin = GetThreadStats(*zx::thread::self());
    ASSERT_TRUE(stats_begin.is_ok());
    EXPECT_EQ(kTestThreadCpu, stats_begin->last_scheduled_cpu);

    // Busy wait to accumulate CPU time over for a little over 10 periods.
    const zx::duration spin_duration = zx::duration(kTestThreadDeadlineParams.period) * 10 +
                                       zx::duration(kTestThreadDeadlineParams.capacity) / 2;
    const zx::time time_end = zx::clock::get_monotonic() + spin_duration;
    zx::time now;
    do {
      now = zx::clock::get_monotonic();
    } while (now < time_end);

    const zx::result<zx_info_thread_stats_t> stats_end = GetThreadStats(*zx::thread::self());
    ASSERT_TRUE(stats_end.is_ok());
    EXPECT_EQ(kTestThreadCpu, stats_end->last_scheduled_cpu);

    const zx::duration total_runtime =
        zx::time(stats_end->total_runtime) - zx::time(stats_begin->total_runtime);

    const zx::duration expected_runtime =
        zx::duration(kTestThreadDeadlineParams.capacity) * 2 * 10 +
        zx::duration(kTestThreadDeadlineParams.capacity) / 2;

    const zx::duration delta_runtime = total_runtime - expected_runtime;

    // Accept at most -5% variation from the expected runtime. Emulated environments may produce
    // much larger apparent runtimes than expected, however, this is acceptable since this test's
    // goal is to detect receiving too little runtime.
    const zx::duration min_delta = expected_runtime * -5 / 100;
    EXPECT_GE(delta_runtime, min_delta);

    // Restore the original performance scale.
    result = zx_system_set_performance_info(resource->get(), ZX_CPU_PERF_SCALE,
                                            original_info.data(), original_info.size());
    ASSERT_OK(result);
  });
  EXPECT_OK(run_thread_result);
}
