// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/load_balancer.h>
#include <lib/load_balancer_percpu.h>
#include <lib/system-topology.h>
#include <lib/unittest/unittest.h>

#include <fbl/mutex.h>
#include <kernel/cpu.h>
#include <kernel/percpu.h>
#include <kernel/scheduler.h>
#include <kernel/thread.h>
#include <ktl/array.h>
#include <ktl/unique_ptr.h>

struct LoadBalancerTestAccess {
  static void SetPerformanceScale(Scheduler* scheduler, SchedPerformanceScale scale) {
    scheduler->performance_scale_ = scale;
  }
};

namespace {
using load_balancer::CpuState;
using load_balancer::LoadBalancer;

class TestingContext {
 public:
  static percpu& Get(cpu_num_t cpu_num) {
    ASSERT(cpu_num < percpus_.size());
    return *percpus_[cpu_num];
  }

  static cpu_num_t CurrentCpu() { return current_cpu_; }

  static ktl::array<ktl::unique_ptr<percpu>, 4> CreatePercpus() {
    ktl::array<ktl::unique_ptr<percpu>, 4> out;
    int id = 0;
    fbl::AllocChecker ac;
    for (auto& ptr : out) {
      ptr = ktl::make_unique<percpu>(&ac, id);
      ASSERT(ac.check());
      percpus_[id] = ptr.get();
      id++;
    }
    return out;
  }

  static void UpdateAll(const CpuState::CpuSet& cpus, zx_duration_t threshold) {
    for (auto& percpu : percpus_) {
      percpu->load_balancer.Update(cpus, threshold);
    }
  }

  template <typename Func>
  static void ForEachPercpu(Func&& func) {
    int id = 0;
    for (percpu* percpu : percpus_) {
      func(id++, percpu);
    }
  }

  // Use mutex to lock accesses. In theory there should only ever be one instance
  // of this test running but since we are using static methods we want to
  // ensure that there is no racing happening.
  static DECLARE_MUTEX(TestingContext) lock_;

  static ktl::array<percpu*, 4> percpus_;
  static cpu_num_t current_cpu_;
};

ktl::array<percpu*, 4> TestingContext::percpus_{0};
cpu_num_t TestingContext::current_cpu_ = 0;
decltype(TestingContext::lock_) TestingContext::lock_;

template <typename T>
bool AllEqual(T* a, T* b, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (a[i] != b[i]) {
      printf("%zu :: expected %u  found %u\n", i, a[i], b[i]);
      return false;
    }
  }
  return true;
}

}  // namespace

// Being static members of this class allow the methods to access private
// members on the Scheduler.
class LoadBalancerTest {
 public:
  // Test with all zero values, bit of a sanity test.
  static bool LoadShedThresholdZero() {
    BEGIN_TEST;
    Guard<fbl::Mutex> guard{&TestingContext::lock_};
    auto percpus = TestingContext::CreatePercpus();

    // Don't set the load averages, which leaves them at 0.

    LoadBalancer<TestingContext> lb;
    lb.Cycle();

    for (size_t i = 0; i < percpus.size(); i++) {
      EXPECT_EQ(0,  // There is no load on the system.
                TestingContext::percpus_[i]->load_balancer.queue_time_threshold());
    }

    END_TEST;
  }

  static bool LoadShedThresholdLowVariance() {
    BEGIN_TEST;

    Guard<fbl::Mutex> guard{&TestingContext::lock_};
    auto percpus = TestingContext::CreatePercpus();

    const auto value = 200;

    TestingContext::percpus_[0]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(value);
    TestingContext::percpus_[1]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(value);
    TestingContext::percpus_[2]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(value);
    TestingContext::percpus_[3]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(value);

    for (size_t i = 0; i < percpus.size(); i++) {
      ASSERT_EQ(ZX_TIME_INFINITE,
                TestingContext::percpus_[i]->load_balancer.queue_time_threshold());
    }

    LoadBalancer<TestingContext> lb;
    lb.Cycle();

    // Threshold should be the mean.
    for (size_t i = 0; i < percpus.size(); i++) {
      EXPECT_EQ(value, TestingContext::percpus_[i]->load_balancer.queue_time_threshold());
    }

    END_TEST;
  }

  static bool LoadShedThresholdHighVariance() {
    BEGIN_TEST;
    Guard<fbl::Mutex> guard{&TestingContext::lock_};
    auto percpus = TestingContext::CreatePercpus();

    const auto value = 800;

    // If all queue times are vastly different than the variance is high and the
    // load shed threshold should be below the mean.
    TestingContext::percpus_[0]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(value);
    TestingContext::percpus_[1]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(0);
    TestingContext::percpus_[2]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(0);
    TestingContext::percpus_[3]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(0);

    for (size_t i = 0; i < percpus.size(); i++) {
      ASSERT_EQ(ZX_TIME_INFINITE,
                TestingContext::percpus_[i]->load_balancer.queue_time_threshold());
    }

    LoadBalancer<TestingContext> lb;
    lb.Cycle();

    // Threshold should be the mean.
    for (size_t i = 0; i < percpus.size(); i++) {
      EXPECT_EQ(value / 4, TestingContext::percpus_[i]->load_balancer.queue_time_threshold());
    }

    END_TEST;
  }

  static bool SelectBigFirst() {
    BEGIN_TEST;

    // Lock the testing context to this testcase.
    Guard<fbl::Mutex> guard{&TestingContext::lock_};
    auto percpus = TestingContext::CreatePercpus();

    for (size_t i = 0; i < percpus.size(); i++) {
      ASSERT_EQ(0, TestingContext::percpus_[i]->load_balancer.target_cpus().cpu_count);
      LoadBalancerTestAccess::SetPerformanceScale(&TestingContext::percpus_[i]->scheduler,
                                                  ffl::FromRatio((i < 2) ? 1 : 2, 2));
    }

    LoadBalancer<TestingContext> lb;
    lb.Cycle();

    uint8_t expected_cpus[] = {2, 3, 0, 1};

    for (size_t i = 0; i < percpus.size(); i++) {
      EXPECT_EQ(4,  // We get all of the cpus.
                TestingContext::percpus_[i]->load_balancer.target_cpus().cpu_count);
      EXPECT_TRUE(AllEqual(
          expected_cpus, TestingContext::percpus_[i]->load_balancer.target_cpus().cpus.data(), 3));
    }

    END_TEST;
  }

  static bool PreferUnloaded() {
    BEGIN_TEST;

    // Lock the testing context to this testcase.
    Guard<fbl::Mutex> guard{&TestingContext::lock_};
    auto percpus = TestingContext::CreatePercpus();

    const auto value = 200;

    TestingContext::percpus_[0]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(value);
    TestingContext::percpus_[1]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(0);
    TestingContext::percpus_[2]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(value);
    TestingContext::percpus_[3]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(value);

    for (size_t i = 0; i < percpus.size(); i++) {
      ASSERT_EQ(0, TestingContext::percpus_[i]->load_balancer.target_cpus().cpu_count);
      LoadBalancerTestAccess::SetPerformanceScale(&TestingContext::percpus_[i]->scheduler,
                                                  ffl::FromRatio((i < 2) ? 1 : 2, 2));
    }

    LoadBalancer<TestingContext> lb;
    lb.Cycle();

    // We expect core 1 to be bumped to the front as it is below the threshold.
    uint8_t expected_cpus[] = {1, 2, 3, 0};

    for (size_t i = 0; i < percpus.size(); i++) {
      EXPECT_EQ(4,  // We get all of the cpus.
                TestingContext::percpus_[i]->load_balancer.target_cpus().cpu_count);
      EXPECT_TRUE(AllEqual(
          expected_cpus, TestingContext::percpus_[i]->load_balancer.target_cpus().cpus.data(), 3));
    }

    END_TEST;
  }

  // Simple test, the last cpu is under threshold so we use it.
  static bool FindCpuLast() {
    BEGIN_TEST;

    // Lock the testing context to this testcase.
    Guard<fbl::Mutex> guard{&TestingContext::lock_};
    auto percpus = TestingContext::CreatePercpus();

    Thread thread;
    thread.scheduler_state().last_cpu_ = 1;
    percpus[1]->load_balancer.Update({}, 10000);
    percpus[1]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(100);

    cpu_num_t selected = load_balancer::FindTargetCpuLocked<TestingContext>(&thread);
    EXPECT_EQ(1u, selected);

    END_TEST;
  }

  // The last cpu is unset so we will use the first cpu in the current
  // processor's list.
  static bool FindCpuInitial() {
    BEGIN_TEST;
    // Lock the testing context to this testcase.
    Guard<fbl::Mutex> guard{&TestingContext::lock_};

    auto percpus = TestingContext::CreatePercpus();
    constexpr auto kCurrCpu = 2;
    TestingContext::current_cpu_ = kCurrCpu;

    Thread thread;
    // thread..last_cpu_ is undefined, like a new thread on the system.

    percpus[kCurrCpu]->load_balancer.Update({.cpus = {3, 2, 1, 0}, .cpu_count = 4}, 10000);
    percpus[kCurrCpu]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(100);

    cpu_num_t selected =
        load_balancer::FindTargetCpuLocked<TestingContext, TestingContext::CurrentCpu>(&thread);
    EXPECT_EQ(3u, selected);

    TestingContext::current_cpu_ = 0;
    END_TEST;
  }

  static bool FindCpuFirstUnderThreshold() {
    BEGIN_TEST;
    constexpr auto kLastCpu = 1;
    const auto kDevAllowed = load_balancer::kAllowedRuntimeDeviation;
    const auto kThreshold = 1'000'000;

    // Lock the testing context to this testcase.
    Guard<fbl::Mutex> guard{&TestingContext::lock_};

    auto percpus = TestingContext::CreatePercpus();

    Thread thread;
    thread.scheduler_state().last_cpu_ = kLastCpu;

    TestingContext::UpdateAll({.cpus = {3, 2, 1, 0}, .cpu_count = 4}, kThreshold);
    percpus[3]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + kDevAllowed);
    percpus[2]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + kDevAllowed);
    percpus[1]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold - 1);
    percpus[0]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(0);

    cpu_num_t selected =
        load_balancer::FindTargetCpuLocked<TestingContext, TestingContext::CurrentCpu>(&thread);
    // Even though 0 is lower 1 is under threshold and earlier in the order so we would use it.
    EXPECT_EQ(1u, selected);

    END_TEST;
  }

  static bool FindCpuLowestLoad() {
    BEGIN_TEST;
    constexpr auto kLastCpu = 1;
    const auto kDevAllowed = load_balancer::kAllowedRuntimeDeviation;
    const auto kThreshold = 1'000'000;

    // Lock the testing context to this testcase.
    Guard<fbl::Mutex> guard{&TestingContext::lock_};

    auto percpus = TestingContext::CreatePercpus();

    Thread thread;
    thread.scheduler_state().last_cpu_ = kLastCpu;

    TestingContext::UpdateAll({.cpus = {3, 2, 1, 0}, .cpu_count = 4}, kThreshold);
    percpus[3]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 2);
    percpus[2]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 1);
    percpus[1]->scheduler.exported_total_expected_runtime_ns_ =
        SchedNs(kThreshold + kDevAllowed + 10);
    percpus[0]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 4);

    cpu_num_t selected =
        load_balancer::FindTargetCpuLocked<TestingContext, TestingContext::CurrentCpu>(&thread);
    EXPECT_EQ(2u, selected);

    END_TEST;
  }

  static bool StayOnCurrentIfWithinDeviation() {
    BEGIN_TEST;
    constexpr cpu_num_t kLastCpu = 1;
    const auto kThreshold = 1'000'000;
    static_assert(kThreshold < load_balancer::kAllowedRuntimeDeviation);

    // Lock the testing context to this testcase.
    Guard<fbl::Mutex> guard{&TestingContext::lock_};

    auto percpus = TestingContext::CreatePercpus();

    Thread thread;
    thread.scheduler_state().last_cpu_ = kLastCpu;

    TestingContext::UpdateAll({.cpus = {3, 2, 1, 0}, .cpu_count = 4}, kThreshold);
    percpus[3]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(0);
    percpus[2]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 1);
    percpus[1]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 100);
    percpus[0]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 4);

    cpu_num_t selected =
        load_balancer::FindTargetCpuLocked<TestingContext, TestingContext::CurrentCpu>(&thread);
    // We should stay on the cpu we started on, even though we have another
    // option that is under the threshold and others that are over but lower
    // than us. In all cases these deviations are less than our allowed
    // deviation.
    EXPECT_EQ(kLastCpu, selected);

    END_TEST;
  }
};

UNITTEST_START_TESTCASE(load_balancer_tests)
UNITTEST("Test load shed threshold with no load.", LoadBalancerTest::LoadShedThresholdZero)
UNITTEST("Test load shed threshold with low variance.",
         LoadBalancerTest::LoadShedThresholdLowVariance)
UNITTEST("Test load shed threshold with high variance.",
         LoadBalancerTest::LoadShedThresholdHighVariance)
UNITTEST("Test Selected cpus, prefer big in big.little", LoadBalancerTest::SelectBigFirst)
UNITTEST("Test Selected cpus, prefer unloaded", LoadBalancerTest::PreferUnloaded)
UNITTEST("Test selecting the last cpu if it is under threshold.", LoadBalancerTest::FindCpuLast)
UNITTEST("Test selecting the current cpus best match if it is under threshold.",
         LoadBalancerTest::FindCpuInitial)
UNITTEST("Test selecting the first cpu from the list that is under the threshold.",
         LoadBalancerTest::FindCpuFirstUnderThreshold)
UNITTEST("Test selecting the cpu with the lowest load.", LoadBalancerTest::FindCpuLowestLoad)
UNITTEST("Test avoiding a move if we are in the allowed deviation.",
         LoadBalancerTest::StayOnCurrentIfWithinDeviation)
UNITTEST_END_TESTCASE(load_balancer_tests, "load_balancer",
                      "Tests for the periodic thread load balancer.")
