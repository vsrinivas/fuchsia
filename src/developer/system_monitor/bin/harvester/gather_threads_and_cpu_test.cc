// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_threads_and_cpu.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dockyard_proxy_fake.h"
#include "info_resource.h"

using ::testing::_;
using ::testing::Eq;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Return;
using ::testing::ReturnRef;

class MockTaskTree : public harvester::TaskTree {
 public:
  MOCK_METHOD(void, Gather, (), (override));

  MOCK_METHOD(const std::vector<Task>&, Jobs, (), (const, override));
  MOCK_METHOD(const std::vector<Task>&, Processes, (), (const, override));
  MOCK_METHOD(const std::vector<Task>&, Threads, (), (const, override));
};

class MockOS : public harvester::OS {
 public:
  MOCK_METHOD(zx_duration_t, HighResolutionNow, (), (override));
  MOCK_METHOD(zx_status_t, GetInfo,
              (zx_handle_t parent, unsigned int children_kind, void* out_buffer,
               size_t buffer_size, size_t* actual, size_t* avail),
              (override));
};

class GatherThreadsAndCpuTest : public ::testing::Test {
 public:
  void SetUp() override {
    EXPECT_CALL(task_tree_, Gather()).Times(1);

    jobs_ = {{1, 1, 0}};
    EXPECT_CALL(task_tree_, Jobs()).WillOnce(ReturnRef(jobs_));
    threads_ = {{2, 2, 1}};
    EXPECT_CALL(task_tree_, Processes()).WillOnce(ReturnRef(processes_));
    threads_ = {{101, 101, 1}, {102, 102, 1}};
    EXPECT_CALL(task_tree_, Threads())
        .Times(2)
        .WillRepeatedly(ReturnRef(threads_));

    EXPECT_CALL(os_, GetInfo(_, Eq(ZX_INFO_CPU_STATS), IsNull(), _, _, _))
        .WillOnce(Invoke(this, &GatherThreadsAndCpuTest::GetCpuStatsCount));
    EXPECT_CALL(os_,
                GetInfo(_, Eq(ZX_INFO_CPU_STATS), NotNull(), _, NotNull(), _))
        .WillOnce(Invoke(this, &GatherThreadsAndCpuTest::GetCpuStatsInfo));

    EXPECT_CALL(os_, GetInfo(_, Eq(ZX_INFO_THREAD), NotNull(), _, _, _))
        .WillRepeatedly(Invoke(this, &GatherThreadsAndCpuTest::GetThreadInfo));
    EXPECT_CALL(os_, GetInfo(_, Eq(ZX_INFO_THREAD_STATS), NotNull(), _, _, _))
        .WillRepeatedly(
            Invoke(this, &GatherThreadsAndCpuTest::GetThreadStatsInfo));
  }

  zx_status_t GetCpuStatsCount(zx_handle_t parent, int children_kind,
                               void* out_buffer, size_t buffer_size,
                               size_t* actual, size_t* avail) {
    if (parent != info_resource_) {
      ADD_FAILURE() << "Passed a handle that was not the info resource.";
      return ZX_ERR_BAD_HANDLE;
    }

    *avail = cpu_stats_.size();

    return ZX_OK;
  }

  zx_status_t GetCpuStatsInfo(zx_handle_t parent, int children_kind,
                              void* out_buffer, size_t buffer_size,
                              size_t* actual, size_t* avail) {
    size_t capacity = buffer_size / sizeof(zx_info_cpu_stats_t);

    if (parent != info_resource_) {
      ADD_FAILURE() << "Passed a handle that was not the info resource.";
      return ZX_ERR_BAD_HANDLE;
    }

    *actual = std::min(cpu_stats_.size(), capacity);
    *avail = cpu_stats_.size();
    memcpy(out_buffer, (void*)cpu_stats_.data(),
           *actual * sizeof(zx_info_cpu_stats_t));

    return ZX_OK;
  }

  zx_status_t GetThreadInfo(zx_handle_t parent, int children_kind,
                            void* out_buffer, size_t buffer_size,
                            size_t* actual, size_t* avail) {
    EXPECT_EQ(buffer_size, sizeof(zx_info_thread_t));

    if (thread_info_.count(parent) == 0) {
      ADD_FAILURE() << "Warning: unexpected handle " << parent;
      return ZX_ERR_BAD_HANDLE;
    }

    memcpy(out_buffer, (void*)&thread_info_.at(parent),
           sizeof(zx_info_thread_t));

    return ZX_OK;
  }

  zx_status_t GetThreadStatsInfo(zx_handle_t parent, int children_kind,
                                 void* out_buffer, size_t buffer_size,
                                size_t* actual, size_t* avail) {
    EXPECT_EQ(buffer_size, sizeof(zx_info_thread_stats_t));

    if (thread_stats_.count(parent) == 0) {
      ADD_FAILURE() << "Warning: unexpected handle " << parent;
      return ZX_ERR_BAD_HANDLE;
    }

    memcpy(out_buffer, (void*)&thread_stats_.at(parent),
           sizeof(zx_info_thread_stats_t));

    return ZX_OK;
  }

  // Get a dockyard path for the given |koid| and |suffix| key.
  std::string KoidPath(uint64_t koid, const std::string& suffix) {
    std::ostringstream out;
    out << "koid:" << koid << ":" << suffix;
    return out.str();
  }

  // Get a dockyard path for the given |koid| and |suffix| key.
  std::string CpuPath(uint64_t cpu, const std::string& suffix) {
    std::ostringstream out;
    out << "cpu:" << cpu << ":" << suffix;
    return out.str();
  }

  uint64_t GetValueForPath(std::string path) {
    uint64_t value;
    EXPECT_TRUE(dockyard_proxy_.CheckValueSent(path, &value));
    return value;
  }

 protected:
  // Mocks.
  MockTaskTree task_tree_;
  MockOS os_;

  // Test data.
  std::vector<zx_info_cpu_stats_t> cpu_stats_;
  harvester::DockyardProxyFake dockyard_proxy_;
  zx_handle_t info_resource_;
  std::vector<harvester::TaskTree::Task> jobs_;
  std::vector<harvester::TaskTree::Task> processes_;
  std::vector<harvester::TaskTree::Task> threads_;
  std::map<zx_handle_t, zx_info_thread_t> thread_info_;
  std::map<zx_handle_t, zx_info_thread_stats_t> thread_stats_;
};

TEST_F(GatherThreadsAndCpuTest, GetsThreadAndCpuInfo) {
  harvester::GatherThreadsAndCpu gatherer(info_resource_, &dockyard_proxy_,
                                          task_tree_, &os_);

  EXPECT_CALL(os_, HighResolutionNow()).WillOnce(Return(100UL));

  cpu_stats_ = {{
    .idle_time = 1,
    .reschedules = 2,
    .context_switches = 3,
    .irq_preempts = 4,
    .preempts = 5,
    .yields = 6,
    .ints = 7,
    .timer_ints = 8,
    .timers = 9,
    .syscalls = 10,
    .reschedule_ipis = 11,
    .generic_ipis = 12
  }};
  thread_info_[101] = { .state = ZX_THREAD_STATE_RUNNING, };
  thread_stats_[101] = { .total_runtime = 0, };
  thread_info_[102] = { .state = ZX_THREAD_STATE_DEAD, };
  thread_stats_[102] = { .total_runtime = 1200, };

  gatherer.Gather();

  EXPECT_EQ(GetValueForPath(CpuPath(0, "reschedules")), 2UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "context_switches")), 3UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "meaningful_irq_preempts")), 4UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "preempts")), 5UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "yields")), 6UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "busy_time")), 99UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "idle_time")), 1UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "external_hardware_interrupts")), 7UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "timer_interrupts")), 8UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "timer_callbacks")), 9UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "syscalls")), 10UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "reschedule_ipis")), 11UL);
  EXPECT_EQ(GetValueForPath(CpuPath(0, "generic_ipis")), 12UL);

  EXPECT_EQ(GetValueForPath(KoidPath(101, "thread_state")),
            ZX_THREAD_STATE_RUNNING);
  EXPECT_EQ(GetValueForPath(KoidPath(101, "cpu_total")), 0UL);
  EXPECT_EQ(GetValueForPath(KoidPath(102, "thread_state")),
            ZX_THREAD_STATE_DEAD);
  EXPECT_EQ(GetValueForPath(KoidPath(102, "cpu_total")), 1200UL);
}
