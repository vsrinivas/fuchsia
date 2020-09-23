// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/storage/watcher_list.h"

#include <lib/fit/function.h>

#include <gtest/gtest.h>

namespace modular {
namespace {

using WatcherClosure = fit::function<WatchInterest()>;

// Tests that calling Notify on an empty WatcherList does nothing.
TEST(WatcherList, NotifyEmpty) {
  WatcherList<WatcherClosure> watcher_list;
  watcher_list.Notify();
}

// Tests that calling Notify on a WatcherList with one watcher calls that watcher.
TEST(WatcherList, NotifyCallsOneWatcher) {
  WatcherList<WatcherClosure> watcher_list;

  bool called{false};
  watcher_list.Add([&called]() {
    called = true;
    return WatchInterest::kStop;
  });

  watcher_list.Notify();

  EXPECT_TRUE(called);
}

// Tests that calling Notify on a WatcherList with multiple watchers calls all watchers.
TEST(WatcherList, NotifyCallsMultipleWatchers) {
  static constexpr auto kExpectedCount = 5;

  WatcherList<WatcherClosure> watcher_list;

  int called_count = 0;
  for (int i = 0; i < kExpectedCount; i++) {
    watcher_list.Add([&called_count]() {
      ++called_count;
      return WatchInterest::kStop;
    });
  }

  watcher_list.Notify();

  EXPECT_EQ(kExpectedCount, called_count);
}

// Tests that calling Notify with arguments passes the args to the watcher.
TEST(WatcherList, NotifyWithArguments) {
  static constexpr auto kExpectedArgValue = 42;
  using WatcherFunc = fit::function<WatchInterest(int)>;

  WatcherList<WatcherFunc> watcher_list;

  bool called{false};
  watcher_list.Add([&called](int arg) {
    called = true;
    EXPECT_EQ(kExpectedArgValue, arg);
    return WatchInterest::kStop;
  });

  watcher_list.Notify(kExpectedArgValue);

  EXPECT_TRUE(called);
}

// Tests that calling Notify on a WatcherList with multiple watchers calls all watchers,
// copying the arguments.
TEST(WatcherList, NotifyCallsMultipleWatchersWithMovedArg) {
  static constexpr auto kExpectedCount = 5;
  static constexpr auto kExpectedArgValue = "arg value";
  using WatcherFunc = fit::function<WatchInterest(std::string)>;

  WatcherList<WatcherFunc> watcher_list;

  int called_count = 0;
  for (int i = 0; i < kExpectedCount; i++) {
    watcher_list.Add([&called_count](std::string arg) {
      ++called_count;
      EXPECT_EQ(kExpectedArgValue, arg);
      return WatchInterest::kStop;
    });
  }

  std::string arg{kExpectedArgValue};
  watcher_list.Notify(std::move(arg));

  EXPECT_EQ(kExpectedCount, called_count);
}

// Tests that a watcher that returns WatchInterest::kStop is removed from the list.
TEST(WatcherList, WatchInterestStop) {
  WatcherList<WatcherClosure> watcher_list;

  int called_count = 0;
  watcher_list.Add([&called_count]() {
    ++called_count;
    return WatchInterest::kStop;
  });

  // The first Notify will remove the watcher from the list.
  watcher_list.Notify();
  watcher_list.Notify();

  EXPECT_EQ(1, called_count);
}

// Tests that a watcher that returns WatchInterest::kContinue stays in the list.
TEST(WatcherList, WatchInterestContinue) {
  WatcherList<WatcherClosure> watcher_list;

  int called_count = 0;
  watcher_list.Add([&called_count]() {
    ++called_count;
    return WatchInterest::kContinue;
  });

  watcher_list.Notify();
  watcher_list.Notify();

  EXPECT_EQ(2, called_count);
}

}  // namespace
}  // namespace modular
