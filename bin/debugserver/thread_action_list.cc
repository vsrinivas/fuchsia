// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "thread_action_list.h"

#include "garnet/lib/debugger_utils/util.h"

#include "lib/fxl/logging.h"

#include "util.h"

// N.B. This file is included in the unittest, which does not
// contain all the inferior-control logic. Therefore do not
// #include server.h.

namespace debugserver {

ThreadActionList::Entry::Entry(ThreadActionList::Action action,
                               zx_koid_t pid,
                               zx_koid_t tid)
    : action_(action), pid_(pid), tid_(tid) {
  FXL_DCHECK(pid_ != 0);
  // A tid value of zero is ok.
}

void ThreadActionList::Entry::set_picked_tid(zx_koid_t tid) {
  FXL_DCHECK(tid != 0);
  FXL_DCHECK(tid_ == 0);
  tid_ = tid;
}

bool ThreadActionList::Entry::Contains(zx_koid_t pid, zx_koid_t tid) const {
  FXL_DCHECK(pid != 0 && pid != kAll);
  FXL_DCHECK(tid != 0 && tid != kAll);
  // A "0" meaning "arbitrary process" is resolved to the current process at
  // construction time. A "0" meaning "arbitrary thread" must be resolved by
  // the caller. If it cannot be resolved it is left as zero, and there is
  // no match.
  FXL_DCHECK(pid_ != 0);
  if (pid != pid_ && pid_ != kAll)
    return false;
  if (tid != tid_ && tid_ != kAll)
    return false;
  return true;
}

bool ThreadActionList::DecodeAction(char c, ThreadActionList::Action* out_action) {
  switch (c) {
    case 'c':
      *out_action = Action::kContinue;
      break;
    case 's':
      *out_action = Action::kStep;
      break;
    default:
      return false;
  }

  return true;
}

const char* ThreadActionList::ActionToString(ThreadActionList::Action action) {
#define CASE_TO_STR(x) \
  case x:              \
    return #x
  switch (action) {
    CASE_TO_STR(Action::kNone);
    CASE_TO_STR(Action::kContinue);
    CASE_TO_STR(Action::kStep);
    default:
      break;
  }
#undef CASE_TO_STR
  return "(unknown)";
}

ThreadActionList::ThreadActionList(const fxl::StringView& str,
                                   zx_koid_t cur_proc) {
  size_t len = str.size();
  size_t s = 0;
  Action default_action = Action::kNone;

  if (len == 0) {
    FXL_LOG(ERROR) << "Empty action string";
    return;
  }

  while (s < len) {
    size_t semi = str.find(';', s);
    size_t n;
    if (semi == str.npos)
      n = len - s;
    else
      n = semi - s;
    if (n == 0) {
      FXL_LOG(ERROR) << "Missing action: " << str;
      return;
    }
    Action action;
    if (!DecodeAction(str[s], &action)) {
      FXL_LOG(ERROR) << "Bad action: " << str;
      return;
    }
    if (n == 1) {
      if (default_action != Action::kNone) {
        FXL_LOG(ERROR) << "Multiple default actions: " << str;
        return;
      }
      default_action = action;
    } else if (str[s + 1] == ':') {
      bool has_pid;
      // TODO(dje): koids are uint64_t
      int64_t pid, tid;
      if (!ParseThreadId(str.substr(s + 2, n - 2), &has_pid, &pid,
                         &tid)) {
        FXL_LOG(ERROR) << "Bad thread id in action: " << str;
        return;
      }
      if ((has_pid && pid <= -2) || tid <= -2) {
        FXL_LOG(ERROR) << "Bad thread id in action: " << str;
        return;
      }
      if (!has_pid || pid == 0)
        pid = cur_proc;
      if (pid == -1 && tid != -1) {
        FXL_LOG(ERROR) << "All processes and one thread: " << str;
        return;
      }
      actions_.push_back(
          Entry(action, pid == -1 ? kAll : pid, tid == -1 ? kAll : tid));
    } else {
      FXL_LOG(ERROR) << "Syntax error in action: " << str;
      return;
    }
    if (semi == str.npos)
      s = len;
    else
      s = semi + 1;
  }

  default_action_ = default_action;
  valid_ = true;
}

ThreadActionList::Action ThreadActionList::GetAction(zx_koid_t pid,
                                                     zx_koid_t tid) const {
  FXL_DCHECK(pick_ones_resolved_);

  for (auto e : actions_) {
    if (e.Contains(pid, tid))
      return e.action();
  }

  return default_action_;
}

}  // namespace debugserver
