// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests DataBlockAssigner behavior.

#include <minfs/writeback.h>
#include <zxtest/zxtest.h>

#include "minfs-private.h"

namespace minfs {
namespace {

// Mock Vnode class to be used in DataBlockAssigner tests.
class MockVnodeMinfs : public DataAssignableVnode, public fbl::Recyclable<MockVnodeMinfs> {
public:
    MockVnodeMinfs() = default;
    ~MockVnodeMinfs() = default;

    void fbl_recycle() final {
        if (recycled_ != nullptr) {
            *recycled_ = true;
        }
    }

    void SetRecycled(bool* recycled) {
        recycled_ = recycled;
        *recycled_ = false;
    }

    void AllocateData(Transaction* transaction) final {
        reserved_ = 0;
    }

    void Reserve(blk_t count) {
        reserved_ += count;
    }

    blk_t GetReserved() const { return reserved_; }

    bool IsDirectory() const { return false; }

private:
    blk_t reserved_ = 0;
    bool* recycled_;
};

// Simple test which enqueues and processes a data block allocation for a single vnode.
TEST(DataAssignerTest, ProcessSingleNode) {
    DataBlockAssigner assigner;
    fbl::RefPtr<MockVnodeMinfs> mock_vnode = fbl::AdoptRef(new MockVnodeMinfs());
    mock_vnode->Reserve(10);
    ASSERT_EQ(10, mock_vnode->GetReserved());
    assigner.EnqueueAllocation(fbl::WrapRefPtr(mock_vnode.get()));
    ASSERT_EQ(10, mock_vnode->GetReserved());
    assigner.Process(nullptr);
    ASSERT_EQ(0, mock_vnode->GetReserved());
}

TEST(DataAssignerTest, CheckVnodeRecycled) {
    fbl::RefPtr<MockVnodeMinfs> mock_vnode = fbl::AdoptRef(new MockVnodeMinfs());
    fbl::RefPtr<DataAssignableVnode> data_vnode = fbl::WrapRefPtr(mock_vnode.get());
    bool recycled;
    mock_vnode->SetRecycled(&recycled);
    ASSERT_FALSE(recycled);
    mock_vnode.reset();
    ASSERT_FALSE(recycled);
    data_vnode.reset();
    ASSERT_TRUE(recycled);
}

} // namespace
} // namespace minfs
