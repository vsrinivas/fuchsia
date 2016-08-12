// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "magma_util/macros.h"
#include "magma_util/refcounted.h"
#include "gtest/gtest.h"

namespace {

class TestRefcounted : public magma::Refcounted {
public:
    static TestRefcounted* Create() { return new TestRefcounted(); }

    bool is_deleted() { return deleted_; }

    void ActualDelete() { delete this; }

private:
    TestRefcounted() : magma::Refcounted("test") {}

    ~TestRefcounted() {}

    // Don't really delete
    void Delete() override
    {
        DASSERT(!deleted_);
        deleted_ = true;
    }

private:
    bool deleted_{};
};

} // namespace

TEST(MagmaUtil, Refcounted)
{
    TestRefcounted* rc = TestRefcounted::Create();
    EXPECT_EQ(rc->is_deleted(), false);
    EXPECT_EQ(rc->Getref(), 1);

    rc->Incref();
    EXPECT_EQ(rc->is_deleted(), false);
    EXPECT_EQ(rc->Getref(), 2);

    rc->Decref();
    EXPECT_EQ(rc->is_deleted(), false);
    EXPECT_EQ(rc->Getref(), 1);

    rc->Decref();
    EXPECT_EQ(rc->is_deleted(), true);

    rc->ActualDelete();
}
