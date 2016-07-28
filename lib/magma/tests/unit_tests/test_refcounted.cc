// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

TEST(Magma, Refcounted)
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
