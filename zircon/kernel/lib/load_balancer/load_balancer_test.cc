// Copyright 2020 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <fbl/mutex.h>
#include <kernel/cpu.h>
#include <kernel/percpu.h>
#include <kernel/scheduler.h>
#include <kernel/thread.h>
#include <ktl/array.h>
#include <ktl/unique_ptr.h>
#include <lib/load_balancer_percpu.h>
#include <lib/unittest/unittest.h>

namespace {
using load_balancer::CpuState;

class TestingContext {
 public:
  static percpu& Get(cpu_num_t cpu_num) {
    ASSERT(cpu_num < percpus_.size());
    return *percpus_[cpu_num];
  }

  static cpu_num_t CurrentCpu() {
    return current_cpu_;
  }

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

template<typename T>
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
  // Simple test, the last cpu is under threshold so we use it.
  static bool FindCpuLast() {
    BEGIN_TEST;

    // Lock the testing context to this testcase.
    Guard<fbl::Mutex> guard{&TestingContext::lock_};
    auto percpus = TestingContext::CreatePercpus();

    Thread thread;
    thread.scheduler_state_.last_cpu_ = 1;
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

    percpus[kCurrCpu]->load_balancer.Update({.cpus = {3,2,1,0}, .cpu_count = 4}, 10000);
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
    const auto kThreshold = 1'000'000;

    // Lock the testing context to this testcase.
    Guard<fbl::Mutex> guard{&TestingContext::lock_};

    auto percpus = TestingContext::CreatePercpus();

    Thread thread;
    thread.scheduler_state_.last_cpu_ = kLastCpu;

    TestingContext::UpdateAll({.cpus = {3,2,1,0}, .cpu_count = 4}, kThreshold);
    percpus[3]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 1);
    percpus[2]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 1);
    percpus[1]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 1);
    percpus[0]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold - 1);

    cpu_num_t selected =
        load_balancer::FindTargetCpuLocked<TestingContext, TestingContext::CurrentCpu>(&thread);
    EXPECT_EQ(0u, selected);

    END_TEST;
  }

  static bool FindCpuLowestLoad() {
    BEGIN_TEST;
    constexpr auto kLastCpu = 1;
    const auto kThreshold = 1'000'000;

    // Lock the testing context to this testcase.
    Guard<fbl::Mutex> guard{&TestingContext::lock_};

    auto percpus = TestingContext::CreatePercpus();

    Thread thread;
    thread.scheduler_state_.last_cpu_ = kLastCpu;

    TestingContext::UpdateAll({.cpus = {3,2,1,0}, .cpu_count = 4}, kThreshold);
    percpus[3]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 2);
    percpus[2]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 1);
    percpus[1]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 3);
    percpus[0]->scheduler.exported_total_expected_runtime_ns_ = SchedNs(kThreshold + 4);

    cpu_num_t selected =
        load_balancer::FindTargetCpuLocked<TestingContext, TestingContext::CurrentCpu>(&thread);
    EXPECT_EQ(2u, selected);

    END_TEST;
  }

};

UNITTEST_START_TESTCASE(load_balancer_tests)
  UNITTEST("Test selecting the last cpu if it is under threshold.",
           LoadBalancerTest::FindCpuLast)
  UNITTEST("Test selecting the current cpus best match if it is under threshold.",
           LoadBalancerTest::FindCpuInitial)
  UNITTEST("Test selecting the first cpu from the list that is under the threshold.",
           LoadBalancerTest::FindCpuFirstUnderThreshold)
  UNITTEST("Test selecting the cpu with the lowest load.",
           LoadBalancerTest::FindCpuLowestLoad)
UNITTEST_END_TESTCASE(load_balancer_tests, "load_balancer",
                      "Tests for the periodic thread load balancer.")

