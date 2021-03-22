// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "os.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::_;
using testing::IsNull;
using testing::NotNull;
using testing::StrictMock;
using testing::Return;

// Used to mock out the zx_* calls for hermetic tests.
class MockOS : public harvester::OS {
 public:
  MOCK_METHOD(zx_duration_t, HighResolutionNow, (), (override));
  MOCK_METHOD(zx_status_t, GetInfo,
              (zx_handle_t parent, unsigned int children_kind, void* out_buffer,
               size_t buffer_size, size_t* actual, size_t* avail),
              (override));
};

class OSTest : public ::testing::Test {
 public:
  zx_status_t GetChildCount(zx_handle_t parent, unsigned int children_kind,
                          void* out_buffer, size_t buffer_size, size_t* actual,
                          size_t* avail) {
    if (handle_to_children_.count(parent) == 0) {
      ADD_FAILURE() << "Warning: unexpected handle " << parent;
      return ZX_ERR_BAD_HANDLE;
    }
    const std::vector<zx_koid_t>& children = handle_to_children_.at(parent);

    *avail = children.size();

    return ZX_OK;
  }

  zx_status_t GetChildInfo(zx_handle_t parent, unsigned int children_kind,
                         void* out_buffer, size_t buffer_size, size_t* actual,
                         size_t* avail) {
    size_t capacity = buffer_size / sizeof(zx_koid_t);

    if (handle_to_children_.count(parent) == 0) {
      ADD_FAILURE() << "Warning: unexpected handle " << parent;
      return ZX_ERR_BAD_HANDLE;
    }
    const std::vector<zx_koid_t>& children = handle_to_children_.at(parent);

    *actual = std::min(children.size(), capacity);
    *avail = children.size();
    memcpy(out_buffer, (void*)children.data(), *actual * sizeof(zx_koid_t));

    return ZX_OK;
  }

 protected:
  StrictMock<MockOS> os_;
  std::unordered_map<zx_handle_t, std::vector<zx_koid_t>> handle_to_children_;
};

TEST_F(OSTest, ReturnsAnError_OnRetrievingCount) {
  handle_to_children_[0] = {101, 102, 103};

  EXPECT_CALL(os_, GetInfo(_, _, IsNull(), _, _, _))
      .WillOnce(Return(ZX_ERR_BAD_STATE));

  std::vector<zx_koid_t> children(10);
  zx_status_t status = os_.GetChildren(0, 0, ZX_INFO_PROCESS_THREADS,
                                       "ZX_INFO_PROCESS_THREADS", children);

  EXPECT_EQ(status, ZX_ERR_BAD_STATE);
  // No expectations beyond this. After a failure the state of children is
  // undefined.
}

TEST_F(OSTest, ReturnsAnError_OnRetrievingChildren) {
  handle_to_children_[0] = {101, 102, 103};

  EXPECT_CALL(os_, GetInfo(_, _, IsNull(), _, _, _))
      .WillOnce(Invoke(this, &OSTest::GetChildCount));
  EXPECT_CALL(os_, GetInfo(_, _, NotNull(), _, NotNull(), _))
      .WillOnce(Return(ZX_ERR_BAD_STATE));

  std::vector<zx_koid_t> children(10);
  zx_status_t status = os_.GetChildren(0, 0, ZX_INFO_PROCESS_THREADS,
                                       "ZX_INFO_PROCESS_THREADS", children);

  EXPECT_EQ(status, ZX_ERR_BAD_STATE);
  // No expectations beyond this. After a failure the state of children is
  // undefined.
}

TEST_F(OSTest, GetsChildren) {
  handle_to_children_[0] = {101, 102, 103};

  EXPECT_CALL(os_, GetInfo(_, _, IsNull(), _, _, _))
      .WillOnce(Invoke(this, &OSTest::GetChildCount));
  EXPECT_CALL(os_, GetInfo(_, _, NotNull(), _, NotNull(), _))
      .WillOnce(Invoke(this, &OSTest::GetChildInfo));

  std::vector<zx_koid_t> children(10);
  zx_status_t status = os_.GetChildren(0, 0, ZX_INFO_PROCESS_THREADS,
                                       "ZX_INFO_PROCESS_THREADS", children);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(children.size(), 3UL);
  for (size_t i = 0; i < children.size(); ++i) {
    EXPECT_EQ(children[i], handle_to_children_[0][i]);
  }
}

TEST_F(OSTest, GrowsChildVectorToFitAvailable) {
  const size_t initial_size = 20;
  const size_t available = 21;

  for (size_t i = 0; i < available; ++i) {
    handle_to_children_[0].push_back(i + 100);
  }

  EXPECT_CALL(os_, GetInfo(_, _, IsNull(), _, _, _))
      .WillOnce(Invoke(this, &OSTest::GetChildCount));
  EXPECT_CALL(os_, GetInfo(_, _, NotNull(), _, NotNull(), _))
      .WillOnce(Invoke(this, &OSTest::GetChildInfo));

  std::vector<zx_koid_t> children(initial_size);
  zx_status_t status = os_.GetChildren(0, 0, ZX_INFO_PROCESS_THREADS,
                                       "ZX_INFO_PROCESS_THREADS", children);

  EXPECT_EQ(status, ZX_OK);
  EXPECT_EQ(children.size(), available);
  for (size_t i = 0; i < children.size(); ++i) {
    EXPECT_EQ(children[i], handle_to_children_[0][i]);
  }
}

