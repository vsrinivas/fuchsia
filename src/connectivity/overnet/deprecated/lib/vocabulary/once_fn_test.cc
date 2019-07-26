// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/vocabulary/once_fn.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::StrictMock;

namespace overnet {
namespace once_fn_test {

typedef std::shared_ptr<int> BoxedInt;
BoxedInt boxed_int() { return BoxedInt(new int(42)); }

class MockCallbackClass {
 public:
  MOCK_METHOD0(Called, void());
};

using TestFn = OnceFn<void(), 256>;

class OnceFnTestCase : public testing::TestWithParam<std::function<TestFn(MockCallbackClass*)>> {};

TEST(OnceFn, CallingEmptyCallbackCrashes) {
  OnceFn<void()> empty;
  EXPECT_DEATH_IF_SUPPORTED(empty(), "");
}

TEST(OnceFn, MustCalledCalledPath) {
  StrictMock<MockCallbackClass> cb;
  OnceFn<void(int)> must_call(MUST_CALL, [&](int i) {
    EXPECT_EQ(i, 1);
    cb.Called();
  });
  EXPECT_CALL(cb, Called());
  must_call(1);
  EXPECT_DEATH_IF_SUPPORTED(must_call(2), "");
}

TEST(OnceFn, MustCalledNotCalledPath) {
  StrictMock<MockCallbackClass> cb;
  OnceFn<void(int)> must_call(MUST_CALL, [&](int i) {
    EXPECT_EQ(i, 0);
    cb.Called();
  });
  EXPECT_CALL(cb, Called());
}

TEST(OnceFn, NotCalledCleansUp) {
  StrictMock<MockCallbackClass> mock;
  int i = 42;
  auto cleanup = [&mock, &i](int* p) {
    EXPECT_EQ(p, &i);
    mock.Called();
  };

  OnceFn<void(), 256> cb([ptr = std::unique_ptr<int, decltype(cleanup)>(&i, cleanup)]() {
    FAIL() << "Should never reach here";
  });

  EXPECT_CALL(mock, Called());
}

TEST_P(OnceFnTestCase, CalledWorks) {
  StrictMock<MockCallbackClass> mock;

  TestFn cb = GetParam()(&mock);

  EXPECT_CALL(mock, Called());
  cb();
  EXPECT_DEATH_IF_SUPPORTED(cb(), "");
}

TEST_P(OnceFnTestCase, MoveWorks) {
  StrictMock<MockCallbackClass> mock;

  TestFn cb = GetParam()(&mock);

  TestFn cb2 = std::move(cb);
  EXPECT_DEATH_IF_SUPPORTED(cb(), "");

  EXPECT_CALL(mock, Called());
  cb2();
}

TEST_P(OnceFnTestCase, MoveAssignWorks) {
  StrictMock<MockCallbackClass> mock;

  TestFn cb = GetParam()(&mock);

  TestFn cb2 = std::move(cb);
  EXPECT_DEATH_IF_SUPPORTED(cb(), "");

  cb = std::move(cb2);
  EXPECT_DEATH_IF_SUPPORTED(cb2(), "");

  EXPECT_CALL(mock, Called());
  cb();
}

INSTANTIATE_TEST_SUITE_P(
    OnceFnTests, OnceFnTestCase,
    testing::Values([](auto* mock) { return [mock]() { mock->Called(); }; },
                    [](auto* mock) {
                      OnceFn<void(int), 128> fn([mock, expect = std::make_unique<int>(1)](int i) {
                        EXPECT_EQ(*expect, i);
                        mock->Called();
                      });
                      fn.AddMutator([](auto fn, int i) { fn(i + 1); });
                      return [fn = std::move(fn)]() mutable { fn(0); };
                    },
                    [](auto* mock) {
                      OnceFn<void(int), 208> fn([mock](int i) {
                        EXPECT_EQ(2, i);
                        mock->Called();
                      });
                      fn.AddMutator([](auto fn, int i) { fn(i + 1); });
                      fn.AddMutator([](auto fn, int i) { fn(i + 1); });
                      return [fn = std::move(fn)]() mutable { fn(0); };
                    },
                    [](auto* mock) {
                      OnceFn<MockCallbackClass*(int), 128> fn([mock](int i) {
                        EXPECT_EQ(1, i);
                        return mock;
                      });
                      fn.AddMutator([](auto fn, int i) { return fn(i + 1); });
                      return [fn = std::move(fn)]() mutable { fn(0)->Called(); };
                    },
                    [](auto* mock) {
                      OnceFn<MockCallbackClass*(int), 128> fn([mock](int i) {
                        EXPECT_EQ(2, i);
                        return mock;
                      });
                      fn.AddMutator([add = std::make_unique<int>(1)](auto fn, int i) {
                        return fn(i + *add);
                      });
                      fn.AddMutator([](auto fn, int i) { return fn(i + 1); });
                      return [fn = std::move(fn)]() mutable { fn(0)->Called(); };
                    }));

}  // namespace once_fn_test
}  // namespace overnet
