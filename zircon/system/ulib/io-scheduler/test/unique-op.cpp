// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include <io-scheduler/io-scheduler.h>

namespace {

using StreamOp = ioscheduler::StreamOp;
using UniqueOp = ioscheduler::UniqueOp;

TEST(UniqueOpTest, CreateNull) {
    UniqueOp ref;     // Null object.
    ASSERT_TRUE(!ref, "Expected false");
    ASSERT_TRUE(ref == nullptr, "Expected null reference");
    StreamOp* op = ref.get();
    ASSERT_EQ(op, nullptr, "Expected null returned from reference");
    op = ref.release();
    ASSERT_EQ(op, nullptr, "Expected null returned from reference");

    // Move construction.
    UniqueOp moved(std::move(ref));
    ASSERT_TRUE(moved == nullptr, "Expected null reference from moved");

    // Move assignment.
    UniqueOp assigned = std::move(moved);
    ASSERT_TRUE(assigned == nullptr, "Expected null reference from assigned");
}

TEST(UniqueOpTest, CreateAllocated) {
    StreamOp* op = new StreamOp;
    UniqueOp ref(op);
    ASSERT_TRUE(ref, "Expected true");
    ASSERT_TRUE(ref != nullptr, "Expected non-null reference");
    ASSERT_EQ(ref.get(), op, "Expected op");

    // Move construction.
    UniqueOp moved(std::move(ref));
    ASSERT_EQ(moved.get(), op, "Expected op from moved");

    // Move assignment.
    UniqueOp assigned = std::move(moved);
    ASSERT_EQ(assigned.get(), op, "Expected op from assigned");

    // Release.
    StreamOp* released = assigned.release();
    ASSERT_EQ(released, op, "Expected op from assigned");
    ASSERT_TRUE(assigned == nullptr, "Expected null reference from assigned");

    delete op;
}

} // namespace
