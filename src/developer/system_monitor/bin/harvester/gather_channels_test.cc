// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "gather_channels.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "dockyard_proxy_fake.h"
#include "info_resource.h"

using ::testing::_;
using ::testing::IsNull;
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

class GatherChannelsTest : public ::testing::Test {
 public:
  void SetUp() override {}

  zx_status_t GetHandleCount(zx_handle_t parent, int children_kind,
                             void* out_buffer, size_t buffer_size,
                             size_t* actual, size_t* avail) {
    if (process_to_handles_.count(parent) == 0) {
      ADD_FAILURE() << "Warning: unexpected handle " << parent;
      return ZX_ERR_BAD_HANDLE;
    }
    const std::vector<zx_info_handle_extended>& handles =
        process_to_handles_.at(parent);

    *avail = handles.size();

    return ZX_OK;
  }

  zx_status_t GetHandleInfo(zx_handle_t parent, int children_kind,
                            void* out_buffer, size_t buffer_size,
                            size_t* actual, size_t* avail) {
    size_t capacity = buffer_size / sizeof(zx_info_handle_extended);

    if (process_to_handles_.count(parent) == 0) {
      ADD_FAILURE() << "Warning: unexpected handle " << parent;
      return ZX_ERR_BAD_HANDLE;
    }
    const std::vector<zx_info_handle_extended>& handles =
        process_to_handles_.at(parent);

    *actual = std::min(handles.size(), capacity);
    *avail = handles.size();
    memcpy(out_buffer, (void*)handles.data(),
           *actual * sizeof(zx_info_handle_extended));

    return ZX_OK;
  }

  // Get a dockyard path for the given |koid| and |suffix| key.
  std::string KoidPath(uint64_t koid, const std::string& suffix) {
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
  MockTaskTree task_tree_;
  MockOS os_;

  // Test data.
  harvester::DockyardProxyFake dockyard_proxy_;
  zx_handle_t info_resource_;
  std::vector<harvester::TaskTree::Task> processes_;
  std::map<zx_handle_t, std::vector<zx_info_handle_extended>>
      process_to_handles_;
};

TEST_F(GatherChannelsTest, FindsChannels) {
  harvester::GatherChannels gatherer(info_resource_, &dockyard_proxy_,
                                     task_tree_, &os_);

  processes_ = {{1, 1, 0}};
  EXPECT_CALL(task_tree_, Processes()).WillOnce(ReturnRef(processes_));

  process_to_handles_[1] = {
    {
      .type = ZX_OBJ_TYPE_CHANNEL,
      .koid = 101,
      .related_koid = 201,
    },
    {
      .type = ZX_OBJ_TYPE_THREAD,
      .koid = 102,
    },
    {
      .type = ZX_OBJ_TYPE_CHANNEL,
      .koid = 103,
    }
  };
  EXPECT_CALL(os_, GetInfo(_, _, IsNull(), _, _, _))
      .WillOnce(Invoke(this, &GatherChannelsTest::GetHandleCount));
  EXPECT_CALL(os_, GetInfo(_, _, NotNull(), _, NotNull(), _))
      .WillOnce(Invoke(this, &GatherChannelsTest::GetHandleInfo));

  gatherer.Gather();

  // Verify that something is being sent.
  EXPECT_TRUE(dockyard_proxy_.CheckValueSubstringSent("type"));
  EXPECT_TRUE(dockyard_proxy_.CheckValueSubstringSent("process"));
  EXPECT_TRUE(dockyard_proxy_.CheckValueSubstringSent("peer"));

  // Verify the 2 channels above are sent, but not the thread.
  EXPECT_EQ(GetValueForPath(KoidPath(101, "type")),
            dockyard::KoidType::CHANNEL);
  EXPECT_EQ(GetValueForPath(KoidPath(101, "process")), 1UL);
  EXPECT_EQ(GetValueForPath(KoidPath(101, "peer")), 201UL);

  uint64_t test_value;
  EXPECT_FALSE(dockyard_proxy_.CheckValueSent(KoidPath(102, "process"),
                                              &test_value));

  EXPECT_EQ(GetValueForPath(KoidPath(103, "type")),
            dockyard::KoidType::CHANNEL);
  EXPECT_EQ(GetValueForPath(KoidPath(103, "process")), 1UL);
  EXPECT_EQ(GetValueForPath(KoidPath(103, "peer")), 0UL);

  // TODO(fxbug.dev/54364): add channel information when dockyard supports multi
  // maps.
  // EXPECT_TRUE(dockyard_proxy.CheckValueSubstringSent("channel"));
}

