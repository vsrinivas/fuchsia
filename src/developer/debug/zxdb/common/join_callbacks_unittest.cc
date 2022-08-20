// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/common/join_callbacks.h"

#include <gtest/gtest.h>

namespace zxdb {

namespace {

// A move-only class for testing the param storage semantics.
struct MoveOnly {
  MoveOnly() = default;
  MoveOnly(int a, int b) : a(a), b(b) {}
  MoveOnly(const MoveOnly&) = delete;
  MoveOnly(MoveOnly&& other) : a(other.a), b(other.b) {}

  MoveOnly& operator=(MoveOnly&& other) {
    a = other.a;
    b = other.b;
    return *this;
  }

  int a = 0;
  int b = 0;
};

}  // namespace

TEST(JoinErrCallbacks, NoCallbacks) {
  int called = 0;
  auto join = fxl::MakeRefCounted<JoinErrCallbacks>([&called](const Err& err) {
    EXPECT_EQ(0, called);
    called++;
    EXPECT_FALSE(err.has_error());
  });

  EXPECT_EQ(0, called);
  join->Ready();
  EXPECT_EQ(1, called);
}

TEST(JoinErrCallbacks, TwoSuccess) {
  int called = 0;
  auto join = fxl::MakeRefCounted<JoinErrCallbacks>([&called](const Err& err) {
    EXPECT_EQ(0, called);
    called++;
    EXPECT_FALSE(err.has_error());
  });

  auto cb1 = join->AddCallback();
  auto cb2 = join->AddCallback();
  join->Ready();
  EXPECT_EQ(0, called);
  cb2(Err());
  EXPECT_EQ(0, called);
  cb1(Err());
  EXPECT_EQ(1, called);
}

TEST(JoinErrCallbacks, FirstFail) {
  int called = 0;
  auto join = fxl::MakeRefCounted<JoinErrCallbacks>([&called](const Err& err) {
    EXPECT_EQ(0, called);
    called++;
    EXPECT_TRUE(err.has_error());
    EXPECT_EQ("First", err.msg());
  });

  auto cb1 = join->AddCallback();
  auto cb2 = join->AddCallback();
  join->Ready();
  cb1(Err("First"));
  EXPECT_EQ(1, called);
  cb2(Err("Second"));
  EXPECT_EQ(1, called);
}

TEST(JoinErrCallbacks, SecondFail) {
  int called = 0;
  auto join = fxl::MakeRefCounted<JoinErrCallbacks>([&called](const Err& err) {
    EXPECT_EQ(0, called);
    called++;
    EXPECT_TRUE(err.has_error());
    EXPECT_EQ("Second", err.msg());
  });

  auto cb1 = join->AddCallback();
  auto cb2 = join->AddCallback();
  join->Ready();
  cb1(Err());
  EXPECT_EQ(0, called);
  cb2(Err("Second"));
  EXPECT_EQ(1, called);
}

TEST(JoinCallbacks, TwoCallbacks) {
  int called = 0;
  auto join = fxl::MakeRefCounted<JoinCallbacks<int>>([&called](std::vector<int> params) {
    EXPECT_EQ(0, called);
    called++;
    EXPECT_EQ(2u, params.size());

    // Params should be in order callbacks were created.
    EXPECT_EQ(100, params[0]);
    EXPECT_EQ(101, params[1]);
  });

  fit::callback<void(int)> cb1 = join->AddCallback();
  fit::callback<void(const int&)> cb2 = join->AddCallback();
  EXPECT_EQ(0, called);
  join->Ready();
  cb2(101);
  EXPECT_EQ(0, called);
  cb1(100);
  EXPECT_EQ(1, called);
}

TEST(JoinCallbacks, AbandonNoIssued) {
  auto join = fxl::MakeRefCounted<JoinCallbacks<int>>([](std::vector<int> params) {
    // Should not be called.
    EXPECT_FALSE(true);
  });
  join->Abandon();
}

// Abandon with a callbacks issued but not ready.
TEST(JoinCallbacks, AbandonIssueNotReady) {
  auto join = fxl::MakeRefCounted<JoinCallbacks<int>>([](std::vector<int> params) {
    // Should not be called.
    EXPECT_FALSE(true);
  });
  fit::callback<void(int)> cb = join->AddCallback();
  join->Abandon();
  cb(1);
}

TEST(JoinCallbacks, AbandonAfterReady) {
  auto join = fxl::MakeRefCounted<JoinCallbacks<int>>([](std::vector<int> params) {
    // Should not be called.
    EXPECT_FALSE(true);
  });
  fit::callback<void(int)> cb = join->AddCallback();
  join->Ready();
  join->Abandon();
  cb(1);
}

TEST(JoinCallbacks, MoveOnly) {
  int called = 0;

  auto join = fxl::MakeRefCounted<JoinCallbacks<MoveOnly>>([&called](std::vector<MoveOnly> params) {
    EXPECT_EQ(0, called);
    called++;
    EXPECT_EQ(2u, params.size());
    EXPECT_EQ(1, params[0].a);
    EXPECT_EQ(2, params[0].b);
    EXPECT_EQ(3, params[1].a);
    EXPECT_EQ(4, params[1].b);
  });

  auto cb1 = join->AddCallback();
  auto cb2 = join->AddCallback();
  cb2(MoveOnly(3, 4));
  join->Ready();
  cb1(MoveOnly(1, 2));

  EXPECT_EQ(1, called);
}

// Tests the specialization for no parameters.
TEST(JoinCallbacks, NoParam) {
  int called = 0;

  auto join = fxl::MakeRefCounted<JoinCallbacks<void>>([&called]() {
    EXPECT_EQ(0, called);
    called++;
  });

  auto cb1 = join->AddCallback();
  auto cb2 = join->AddCallback();
  cb2();
  join->Ready();
  cb1();

  EXPECT_EQ(1, called);
}

}  // namespace zxdb
