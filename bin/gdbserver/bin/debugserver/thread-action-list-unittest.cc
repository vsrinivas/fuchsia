// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread-action-list.h"

#include "gtest/gtest.h"

namespace debugserver {
namespace {

constexpr size_t kMaxActions = 4;
constexpr mx_koid_t kCurProc = 42;
constexpr mx_koid_t kMinusOne = ~0ull;

struct Action {
  ThreadActionList::Action action;
  mx_koid_t pid, tid;
};

struct ActionTest {
  bool ok;
  const char* str;
  ThreadActionList::Action default_action;
  size_t nr_actions;
  Action actions[kMaxActions];
};

#define CONTINUE ThreadActionList::Action::kContinue
#define NONE ThreadActionList::Action::kNone

const ActionTest basic_tests[] = {
    {true, "c", CONTINUE, 0, {}},
    {true, "c;", CONTINUE, 0, {}},
    {true, "c:p1.1", NONE, 1, {{CONTINUE, 1, 1}}},
    {true, "c:3", NONE, 1, {{CONTINUE, kCurProc, 3}}},
    {true, "c:p-1.-1", NONE, 1, {{CONTINUE, kMinusOne, kMinusOne}}},
    {true,
     "c;c:p1.-1;c:p2.3",
     CONTINUE,
     2,
     {{CONTINUE, 1, kMinusOne}, {CONTINUE, 2, 3}}},
    {true, "c:p0.0", NONE, 1, {{CONTINUE, kCurProc, 0}}},

    {false, "", NONE, 0, {}},
    {false, "?", NONE, 0, {}},
    {false, "c?", NONE, 0, {}},
    {false, "c:?", NONE, 0, {}},
    {false, "c;;", NONE, 0, {}},
    {false, "c:p.3", NONE, 0, {}},
    // Multiple default actions is an error.
    {false, "c;c", NONE, 0, {}},
    // Specifying all processes and a specific thread is an error.
    {false, "c:p-1.1", NONE, 0, {}},
};

TEST(ThreadActionListTest, Basic) {
  for (size_t i = 0; i < countof(basic_tests); ++i) {
    const ActionTest* t = &basic_tests[i];
    FXL_LOG(INFO) << "Testing \"" << t->str << "\"";
    ThreadActionList actions(fxl::StringView(t->str), kCurProc);
    EXPECT_EQ(t->ok, actions.valid());
    if (t->ok) {
      EXPECT_EQ(t->default_action, actions.default_action());
      auto v = actions.actions();
      EXPECT_EQ(t->nr_actions, v.size());
      if (t->nr_actions == v.size()) {
        for (size_t j = 0; j < t->nr_actions; ++j) {
          EXPECT_EQ(t->actions[j].action, v[j].action());
          EXPECT_EQ(t->actions[j].pid, v[j].pid());
          EXPECT_EQ(t->actions[j].tid, v[j].tid());
        }
      }
    }
  }
}

}  // anonymous namespace
}  // namespace debugserver
