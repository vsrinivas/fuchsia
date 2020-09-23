// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORAGE_WATCHER_LIST_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORAGE_WATCHER_LIST_H_

#include <lib/fit/function.h>
#include <lib/fit/traits.h>

#include <list>

#include "src/lib/fxl/macros.h"

namespace modular {

// Return value for |WatcherList| callables that indicate their interest in receiving
// further notifications.
enum class WatchInterest {
  // Indicates the function wishes to be removed from the list of watchers and
  // should not be called again.
  kStop = 0,
  // Indicates the function wishes to continue receiving watch notifications.
  kContinue = 1,
};

// A |WatcherList| is a container of callables that have the ability to remove themselves from
// the list when called.
//
// The callables must return |WatchInterest| to signal whether they wish to be notified
// next time the list is notified.
template <typename Callable,
          typename = std::enable_if_t<std::is_same<
              typename fit::callable_traits<Callable>::return_type, WatchInterest>::value>>
class WatcherList {
 public:
  WatcherList() = default;

  // Add a watcher to the list.
  void Add(Callable watcher) { watchers_.push_back(std::move(watcher)); }

  // Notify all watchers in the list.
  //
  // All |args| must be copyable.
  template <typename... Args>
  void Notify(Args&&... args) {
    // Call all of the watchers with the given arguments.
    for (auto it = watchers_.begin(); it != watchers_.end();) {
      auto& watcher = *it;
      auto result = watcher(args...);

      // Remove the watcher if indicated that it wishes to be removed.
      if (result == WatchInterest::kStop) {
        it = watchers_.erase(it);
      } else {
        ++it;
      }
    }
  }

 private:
  std::list<Callable> watchers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(WatcherList);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORAGE_WATCHER_LIST_H_
