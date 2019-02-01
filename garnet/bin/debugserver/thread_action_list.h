// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <vector>

#include <zircon/types.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

namespace debugserver {

class Server;
class Thread;

// Utility class for holding the thread action list argument to vCont packets.
// https://sourceware.org/gdb/current/onlinedocs/gdb/Packets.html

class ThreadActionList final {
 public:
  // The kind of action to perform.
  enum class Action {
    // No action specified for this thread.
    kNone,
    // Continue the thread.
    kContinue,
    // Step the thread one instruction.
    kStep,
    // Other actions are not supported yet.
  };

  // Utility class to hold one entry in ThreadActionList.
  class Entry final {
   public:
    Entry(Action action, zx_koid_t pid, zx_koid_t tid);
    ~Entry() = default;

    Action action() const { return action_; }
    zx_koid_t pid() const { return pid_; }
    zx_koid_t tid() const { return tid_; }

    // Call this to upgrade a "pick one" entry (tid == 0) to the chosen value.
    void set_picked_tid(zx_koid_t tid);

    bool Contains(zx_koid_t pid, zx_koid_t tid) const;

   private:
    friend class ThreadActionList;
    Action action_;
    // N.B. While the remote protocol defines zero for pid/tid to mean "pick a
    // random one" zero values do not end up in |pid_|. The "pick one" must be
    // done before an Entry is created. The "pick one" for |tid_| is resolved
    // later though, after the Entry is created.
    zx_koid_t pid_;
    zx_koid_t tid_;
  };

  // For pid,tid values, means "all processes" or "all threads".
  // TODO(dje): This is a legitimate value, we "should" use a different value,
  // but this is fine for now. The kernel reserves the first 1K, possibly we
  // could use one of those.
  static constexpr zx_koid_t kAll = ~0ull;

  static bool DecodeAction(char c, Action* out_action);

  static const char* ActionToString(Action action);

  ThreadActionList(const fxl::StringView& str, zx_koid_t cur_proc);

  ~ThreadActionList() = default;

  bool valid() const { return valid_; }

  // Call this after resolving all zero tid values, which means to "pick one".
  // This must be called before calling GetAction.
  // This exists to force caller to resolve zero tids ("pick one") to keep
  // the resolution code separate. That step may need to evolve. Plus we'd
  // have to stub out the resolution code in the unittest. Later.
  void MarkPickOnesResolved() { pick_ones_resolved_ = true; }

  // Return the action for |thread|.
  Action GetAction(zx_koid_t pid, zx_koid_t tid) const;

  Action default_action() const { return default_action_; }
  const std::vector<Entry>& actions() const { return actions_; }

 private:
  ThreadActionList() = default;

  // True if the contents are valid.
  bool valid_ = false;

  // True if "pick one" tid values have been resolved.
  bool pick_ones_resolved_ = false;

  Action default_action_ = Action::kNone;
  std::vector<Entry> actions_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ThreadActionList);
};

}  // namespace debugserver
