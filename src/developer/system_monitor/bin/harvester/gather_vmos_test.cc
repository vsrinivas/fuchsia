// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_vmos.h"

#include <zircon/process.h>

#include <algorithm>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dockyard_proxy_fake.h"
#include "info_resource.h"

using ::testing::_;
using ::testing::IsNull;
using ::testing::Le;
using ::testing::NiceMock;
using ::testing::NotNull;
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
  MOCK_METHOD(zx_status_t, GetInfo,
              (zx_handle_t parent, int children_kind, void* out_buffer,
               size_t buffer_size, size_t* actual, size_t* avail),
              (override));
};

class GatherVmosTest : public ::testing::Test {
 public:
  void SetUp() override {
    ASSERT_EQ(harvester::GetInfoResource(&info_resource_), ZX_OK);
  }

  zx_info_vmo_t MakeVmo(zx_koid_t vmo_koid, size_t size_bytes,
                        size_t committed_bytes, const char* name) {
    return MakeVmoWithParent(vmo_koid, 0, size_bytes, committed_bytes, name);
  }

  zx_info_vmo_t MakeVmoWithParent(zx_koid_t vmo_koid, size_t parent_vmo_koid,
                                  size_t size_bytes, size_t committed_bytes,
                                  const char* name) {
    zx_info_vmo_t vmo = {
        .koid = vmo_koid,
        .size_bytes = size_bytes,
        .parent_koid = parent_vmo_koid,
        .committed_bytes = committed_bytes,
    };
    strlcpy(vmo.name, name, sizeof(vmo.name));
    return vmo;
  }

  zx_status_t GetVmoCount(zx_handle_t parent, int children_kind,
                          void* out_buffer, size_t buffer_size, size_t* actual,
                          size_t* avail) {
    if (process_handle_to_vmos_.count(parent) == 0) {
      ADD_FAILURE() << "Warning: unexpected handle " << parent;
      return ZX_ERR_BAD_HANDLE;
    }
    const std::vector<zx_info_vmo_t>& vmos = process_handle_to_vmos_.at(parent);

    *avail = vmos.size();

    return ZX_OK;
  }

  zx_status_t GetVmoInfo(zx_handle_t parent, int children_kind,
                         void* out_buffer, size_t buffer_size, size_t* actual,
                         size_t* avail) {
    size_t capacity = buffer_size / sizeof(zx_info_vmo_t);

    if (process_handle_to_vmos_.count(parent) == 0) {
      ADD_FAILURE() << "Warning: unexpected handle " << parent;
      return ZX_ERR_BAD_HANDLE;
    }
    const std::vector<zx_info_vmo_t>& vmos = process_handle_to_vmos_.at(parent);

    *actual = std::min(vmos.size(), capacity);
    *avail = vmos.size();
    memcpy(out_buffer, (void*)vmos.data(), *actual * sizeof(zx_info_vmo_t));

    return ZX_OK;
  }

  // Get a dockyard path for |koid| with the given |suffix| key.
  std::string KoidPath(zx_koid_t koid, const std::string& suffix) {
    std::ostringstream out;
    out << "koid:" << koid << ":" << suffix;
    return out.str();
  }

  uint64_t GetValueForPath(std::string path) {
    uint64_t value;
    EXPECT_TRUE(dockyard_proxy_.CheckValueSent(path, &value));
    return value;
  }

 protected:
  // Mocks.
  NiceMock<MockTaskTree> task_tree_;
  NiceMock<MockOS> os_;

  // Test data.
  harvester::DockyardProxyFake dockyard_proxy_;
  std::vector<harvester::TaskTree::Task> processes_;
  std::map<zx_handle_t, std::vector<zx_info_vmo_t>> process_handle_to_vmos_;
  zx_handle_t info_resource_;
};

TEST_F(GatherVmosTest, NoRootedVmos) {
  harvester::GatherVmos gatherer(info_resource_, &dockyard_proxy_, task_tree_,
                                 &os_);

  // Build a task tree of:
  //
  //      1 (Root job)
  //     / \
  //    2   5
  //   / \
  //  3   4
  //
  // Where everything but 1 is a process.
  processes_ = {
      // These tuples are {handle, koid, parent koid}.
      // The top level parent 1 is hidden because it's not a process.
      {2, 2, 1},
      {3, 3, 2},
      {4, 4, 2},
      {5, 5, 1},
  };
  ON_CALL(task_tree_, Processes()).WillByDefault(ReturnRef(processes_));

  // Add one VMO for each process, none of which are rooted.
  for (auto& process : processes_) {
    zx_info_vmo_t vmo = MakeVmo(100 + process.koid, 4096, 4096, "scudo");
    process_handle_to_vmos_[process.handle] = {vmo};
  }

  ON_CALL(os_, GetInfo(_, _, IsNull(), _, _, _))
      .WillByDefault(Invoke(this, &GatherVmosTest::GetVmoCount));
  ON_CALL(os_, GetInfo(_, _, NotNull(), _, NotNull(), _))
      .WillByDefault(Invoke(this, &GatherVmosTest::GetVmoInfo));

  gatherer.Gather();

  uint64_t test_value;
  for (auto& process : processes_) {
    EXPECT_TRUE(dockyard_proxy_.CheckValueSent(
        KoidPath(process.koid, "vmo_Sysmem-core"), &test_value));
    EXPECT_EQ(test_value, 0UL);
    EXPECT_TRUE(dockyard_proxy_.CheckValueSent(
        KoidPath(process.koid, "vmo_Sysmem-contig-core"), &test_value));
    EXPECT_EQ(test_value, 0UL);
  }
}

TEST_F(GatherVmosTest, RootedVmos_WithNestedDescendants) {
  harvester::GatherVmos gatherer(info_resource_, &dockyard_proxy_, task_tree_,
                                 &os_);

  // Build a task tree of:
  //
  //      1 (Root job)
  //     / \
  //    2   5
  //   / \
  //  3   4
  //
  // Where everything but 1 is a process.
  processes_ = {
      // These tuples are {handle, koid, parent koid}.
      // The top level parent 1 is hidden because it's not a process.
      {2, 2, 1},
      {3, 3, 2},
      {4, 4, 2},
      {5, 5, 1},
  };
  ON_CALL(task_tree_, Processes()).WillByDefault(ReturnRef(processes_));

  // <koid:5> will hold the rooted VMO.
  zx_info_vmo_t root_vmo = MakeVmo(105, 4096 * 4, 4096 * 4, "Sysmem-core");
  process_handle_to_vmos_[5] = {root_vmo};

  // <koid:2> will take one page to hand out later.
  zx_info_vmo_t intermediate_vmo =
      MakeVmoWithParent(102, root_vmo.koid, 4096, 4096, "Sysmem-core");
  zx_info_vmo_t nonrooted_vmo = MakeVmo(202, 4096 * 2, 4096 * 2, "scudo");
  process_handle_to_vmos_[2] = {intermediate_vmo, nonrooted_vmo};

  // <koid:4> will take the page from <koid:2>. Even though the VMO has been
  // assigned a slightly different name, GatherVmos will still find it using the
  // parent/child relationship.
  zx_info_vmo_t child_vmo = MakeVmoWithParent(104, intermediate_vmo.koid, 4096,
                                              4096, "Sysmem-core-child");
  process_handle_to_vmos_[4] = {child_vmo};

  // <koid:3> gets a non-rooted page.
  zx_info_vmo_t nonrooted_child_vmo =
      MakeVmoWithParent(203, nonrooted_vmo.koid, 4096, 4096, "scudo");
  process_handle_to_vmos_[3] = {nonrooted_child_vmo};

  ON_CALL(os_, GetInfo(_, _, IsNull(), _, _, _))
      .WillByDefault(Invoke(this, &GatherVmosTest::GetVmoCount));
  ON_CALL(os_, GetInfo(_, _, NotNull(), _, NotNull(), _))
      .WillByDefault(Invoke(this, &GatherVmosTest::GetVmoInfo));

  gatherer.Gather();

  // Check that <koid:1> has nothing sent since it's a job.
  uint64_t test_value;
  EXPECT_FALSE(dockyard_proxy_.CheckValueSent(KoidPath(1, "vmo_Sysmem-core"),
                                              &test_value));

  // Check that scudo information is not sent (it is not rooted memory).
  EXPECT_FALSE(
      dockyard_proxy_.CheckValueSent(KoidPath(1, "vmo_scudo"), &test_value));
  EXPECT_FALSE(
      dockyard_proxy_.CheckValueSent(KoidPath(1, "vmo_scudo"), &test_value));

  // <koid:5> should retain 3 rooted pages, <koid:2> and <koid:3> none, and
  // <koid:4> one page.
  EXPECT_EQ(GetValueForPath(KoidPath(2, "vmo_Sysmem-core")), 0UL);
  EXPECT_EQ(GetValueForPath(KoidPath(3, "vmo_Sysmem-core")), 0UL);
  EXPECT_EQ(GetValueForPath(KoidPath(4, "vmo_Sysmem-core")), 4096UL);
  EXPECT_EQ(GetValueForPath(KoidPath(5, "vmo_Sysmem-core")), 12288UL);

  // No processes should have memory from a *different* sysmem VMO.
  for (auto& process : processes_) {
    EXPECT_EQ(GetValueForPath(KoidPath(process.koid, "vmo_Sysmem-contig-core")),
              0UL);
  }
}

TEST_F(GatherVmosTest, TracksChangesOverTime) {
  harvester::GatherVmos gatherer(info_resource_, &dockyard_proxy_, task_tree_,
                                 &os_);

  // Build a list of processes all under root job 0.
  processes_ = {
      // These tuples are {handle, koid, parent koid}.
      // The top level parent 0 is hidden because it's not a process.
      {1, 1, 0}, {2, 2, 0}, {3, 3, 0}, {4, 4, 0}, {5, 5, 0}, {6, 6, 0},
  };
  ON_CALL(task_tree_, Processes()).WillByDefault(ReturnRef(processes_));

  // On first (full) scan, no processes have VMOs.
  for (auto& process : processes_) {
    process_handle_to_vmos_[process.handle] = {};
  }

  ON_CALL(os_, GetInfo(_, _, IsNull(), _, _, _))
      .WillByDefault(Invoke(this, &GatherVmosTest::GetVmoCount));
  ON_CALL(os_, GetInfo(_, _, NotNull(), _, NotNull(), _))
      .WillByDefault(Invoke(this, &GatherVmosTest::GetVmoInfo));

  // Processes to be scanned: all.
  gatherer.Gather();

  // No processes should have sysmem VMOs.
  for (auto& process : processes_) {
    EXPECT_EQ(GetValueForPath(KoidPath(process.koid, "vmo_Sysmem-core")), 0UL);
  }

  // Give <koid:2> a rooted VMO.
  zx_info_vmo_t core_vmo = MakeVmo(102, 4096, 4096, "Sysmem-core");
  process_handle_to_vmos_[2] = {core_vmo};

  // Kill <koid:4>.
  processes_.erase(processes_.begin() + 3);

  // Scan queue: [4, 5, 6, 1, 2, 3]. Gather() will ignore the dead <koid:4> and
  // pull 3 processes [5, 6, 1].
  gatherer.Gather();

  // Only the first 3 processes should be checked; <koid:2>'s new sysmem VMO
  // should not be detected yet.
  for (auto& process : processes_) {
    EXPECT_EQ(GetValueForPath(KoidPath(process.koid, "vmo_Sysmem-core")), 0UL);
  }

  // Add a new <koid:7> with a rooted VMO.
  processes_.push_back({7, 7, 0});
  zx_info_vmo_t contig_vmo = MakeVmo(107, 4096, 4096, "Sysmem-contig-core");
  process_handle_to_vmos_[7] = {contig_vmo};

  // Scan queue: [2, 3, 5, 6, 1, 7]. Gather will see <koid:2>'s VMO. New
  // processes are always scanned.
  gatherer.Gather();

  EXPECT_EQ(GetValueForPath(KoidPath(1, "vmo_Sysmem-core")), 0UL);
  EXPECT_EQ(GetValueForPath(KoidPath(2, "vmo_Sysmem-core")), 4096UL);
  EXPECT_EQ(GetValueForPath(KoidPath(3, "vmo_Sysmem-core")), 0UL);
  EXPECT_EQ(GetValueForPath(KoidPath(5, "vmo_Sysmem-core")), 0UL);
  EXPECT_EQ(GetValueForPath(KoidPath(6, "vmo_Sysmem-core")), 0UL);
  EXPECT_EQ(GetValueForPath(KoidPath(7, "vmo_Sysmem-core")), 0UL);
  EXPECT_EQ(GetValueForPath(KoidPath(7, "vmo_Sysmem-contig-core")), 4096UL);
}
