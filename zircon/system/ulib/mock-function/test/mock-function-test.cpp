// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/mock-function/mock-function.h>
#include <zxtest/zxtest.h>

namespace mock_function {

class MoveOnlyClass {
public:
    DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(MoveOnlyClass);

    MoveOnlyClass() : key_(0) {}
    MoveOnlyClass(uint32_t key) : key_(key) {}

    MoveOnlyClass(MoveOnlyClass&& other) : key_(other.key_) { other.key_ = 0; }
    MoveOnlyClass& operator=(MoveOnlyClass&& other) {
        key_ = other.key_;
        other.key_ = 0;
        return *this;
    }

    bool operator==(const MoveOnlyClass& lhs) const { return lhs.key_ == key_; }

    uint32_t key() const { return key_; }

private:
    uint32_t key_;
};

TEST(MockFunction, MoveArgument) {
    MockFunction<void, MoveOnlyClass, int> mock_function;
    MoveOnlyClass arg1(10);
    MoveOnlyClass arg2(10);

    mock_function.ExpectCall(std::move(arg1), 25);
    mock_function.Call(std::move(arg2), 25);

    mock_function.VerifyAndClear();
}

TEST(MockFunction, MoveReturnValue) {
    MockFunction<MoveOnlyClass, int, int> mock_function;
    MoveOnlyClass arg1(50);

    mock_function.ExpectCall(std::move(arg1), 100, 200);
    MoveOnlyClass ret = mock_function.Call(100, 200);

    mock_function.VerifyAndClear();
    EXPECT_EQ(ret.key(), 50);
}

TEST(MockFunction, MoveTupleReturnValue) {
    MockFunction<std::tuple<int, MoveOnlyClass>, int> mock_function;
    MoveOnlyClass arg1(30);

    mock_function.ExpectCall({80, std::move(arg1)}, 5000);
    std::tuple<int, MoveOnlyClass> tup = mock_function.Call(5000);
    MoveOnlyClass ret = std::move(std::get<1>(tup));

    mock_function.VerifyAndClear();
    EXPECT_EQ(std::get<0>(tup), 80);
    EXPECT_EQ(ret.key(), 30);
}

}  // namespace mock_function
