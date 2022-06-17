// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/pretty_stack_manager.h"

#include <algorithm>

#include "src/developer/debug/zxdb/client/stack.h"

namespace zxdb {

void PrettyStackManager::SetMatchers(std::vector<StackGlob> matchers) {
  matchers_ = std::move(matchers);

  // The matchers must always go from the largest to the smallest.
  std::sort(matchers_.begin(), matchers_.end(), [](const auto& left, const auto& right) {
    return left.frames.size() > right.frames.size();
  });
}

// TODO(bug 43549) this should be loaded from a configuration file somehow associated with the
// user's build instead of being hardcoded.
void PrettyStackManager::LoadDefaultMatchers() {
  std::vector<StackGlob> matchers;

  // C async loop waiting.
  StackGlob c_async_loop(
      "Waiting for event in async_loop_run()",
      {PrettyFrameGlob::Wildcard(1, 1),  // syscalls-<platform>.S
       PrettyFrameGlob::Func("_zx_port_wait"), PrettyFrameGlob::Func("async_loop_run_once"),
       PrettyFrameGlob::Func("async_loop_run")});

  // C++ async loop waiting (just adds a call to the C version).
  StackGlob cpp_async_loop("Waiting for event in async::Loop::Run()", c_async_loop.frames);
  cpp_async_loop.frames.push_back(PrettyFrameGlob::Func("async::Loop::Run"));

  matchers.push_back(std::move(cpp_async_loop));
  matchers.push_back(std::move(c_async_loop));

  // Turn off formatting. We want each frame matcher to appear on different lines and clang-format
  // likes to combine them which makes it more difficult to follow.
  // clang-format off

  // Typical background thread startup.
  StackGlob pthread_startup("pthread startup", {PrettyFrameGlob::Func("start_pthread"),
                                                PrettyFrameGlob::Func("thread_trampoline")});

  // std::thread startup (wraps pthread startup). This has a crazy function name the matcher can't
  // support so accept anything from std::thread being called from pthread startup.
  StackGlob std_thread_startup("std::thread startup", {PrettyFrameGlob::File("thread")});
  std_thread_startup.frames.insert(std_thread_startup.frames.end(), pthread_startup.frames.begin(),
                                   pthread_startup.frames.end());

  matchers.push_back(std::move(pthread_startup));
  matchers.push_back(std::move(std_thread_startup));

  // Async loop thread startup. Don't count "async_loop_run" because we count that as part of runing
  // and task dispatch (dispatch will be the same with a loop and without).
  matchers.push_back(StackGlob("async_loop thread startup",
                               {PrettyFrameGlob::FuncFile("async_loop_run_thread", "loop.c"),
                                PrettyFrameGlob::FuncFile("start_c11", "pthread_create.c"),
                                PrettyFrameGlob::Func("thread_trampoline")}));

  // Async loop task dispatch.
  matchers.push_back(StackGlob("Dispatching task from async loop",
                               {PrettyFrameGlob::Func("async_loop_dispatch_task"),
                                PrettyFrameGlob::Func("async_loop_dispatch_tasks"),
                                PrettyFrameGlob::Func("async_loop_run")}));

  // fpromise::promise and fit::function occur a lot and generate extremely long and useless names.
  // Matching useful sequences is difficult. But just replacing individual stack entries with
  // a simple string eliminates ~3 lines of template goop and ~3 lines of unnecessary function
  // parameters. This makes backtraces much easier to read. Duplicate matches will be merged
  // automatically.
  matchers.push_back(
      StackGlob("fpromise::promise code", {PrettyFrameGlob::File("fit/promise_internal.h")}));
  matchers.push_back(StackGlob("fpromise::promise code", {PrettyFrameGlob::File("fit/promise.h")}));
  matchers.push_back(StackGlob("fit::function code", {PrettyFrameGlob::File("fit/function.h")}));
  matchers.push_back(
      StackGlob("fit::function code", {PrettyFrameGlob::File("fit/function_internal.h")}));

  // Rust async loop waiting.
  StackGlob rust_async_loop(
      "Waiting for event in Executor::run_singlethreaded()",
      {PrettyFrameGlob::Wildcard(1, 1),  // syscalls file (name depends on platform).
       PrettyFrameGlob::Func("_zx_port_wait"),
       PrettyFrameGlob::Func("fuchsia_zircon::port::Port::wait"),
       PrettyFrameGlob::Wildcard(2, 2),  // Lambdas
       PrettyFrameGlob::Func("std::thread::local::LocalKey<*>::try_with<*>"),
       PrettyFrameGlob::Func("std::thread::local::LocalKey<*>::with<*>"),
       PrettyFrameGlob::Func("fuchsia_async::runtime::fuchsia::executor::with_local_timer_heap<*>"),
       PrettyFrameGlob::Func(
           "fuchsia_async::runtime::fuchsia::executor::Executor::run_singlethreaded<*>")});
  matchers.push_back(std::move(rust_async_loop));

  // C startup code. The functions depends on the platform, so just match the file name for most of
  // them. The number of functions in __libc_start_main has varied between 1 and 2 over time. Since
  // these aren't likely to be re-used in other places we can have very general matchers here. The
  // duplicate "libc startup" entries will be merged to produce just one entry.
  PrettyFrameGlob libc_start_main = PrettyFrameGlob::File("__libc_start_main.c");
  PrettyFrameGlob libc_start = PrettyFrameGlob::Func("_start");
  matchers.push_back(StackGlob("libc startup", {libc_start_main}));
  matchers.push_back(StackGlob("libc startup", {libc_start}));

  // Rust has placeholder symbols in the stack "__rust_begin_short_backtrace" and
  // "__rust_end_short_backtrace" which are designed to help clean up backtraces.
  //
  // Rust uses the "begin" to indicate that the stack now contains "good" stack entries (the startup
  // code is complete) and then "end" before the internal crash code. But since we're walking the
  // stack in the opposite direction (most recent first), anything between "end" and "begin" should
  // be removed.
  //
  // This first one matches the "top of stack" crash handing code and later we've got specific
  // matches for the "bottom of stack" code. Each has an end-point to stop matching to avoid
  // overmatching. A more general entry would just list "*" followed by the "end" indicator, but
  // that will be much slower to match against. If we find a more general match is needed, it would
  // be best to hardcode this rust annotation scheme rather than try to express this with globs.
  matchers.push_back(StackGlob(
      "Rust library",
      {PrettyFrameGlob::Func("abort"),
       PrettyFrameGlob::Wildcard(0, 16),
       PrettyFrameGlob::Func("std::sys_common::backtrace::__rust_end_short_backtrace<*>")}));

  // Rust startup code. The "call_once()" in function.rs is present in debug mode but not release.
  matchers.push_back(StackGlob(
      "Rust startup",
      {PrettyFrameGlob::File("rustlib/src/rust/library/core/src/ops/function.rs"),
       PrettyFrameGlob::Func("std::sys_common::backtrace::__rust_begin_short_backtrace<*>"),
       PrettyFrameGlob::Wildcard(0, 16),
       libc_start}));
  matchers.push_back(StackGlob(
      "Rust startup",
      {PrettyFrameGlob::Func("std::sys_common::backtrace::__rust_begin_short_backtrace<*>"),
       PrettyFrameGlob::Wildcard(0, 16),
       libc_start}));

  // Rust new thread code. At least our Rust implementation often adds a bunch of stuff with
  // Executors and LocalKeys that aren't matched by this. It would be nice to elide those also but I
  // don't know how stable those symbols are.
  matchers.push_back(StackGlob(
      "Rust thread startup",
      {PrettyFrameGlob::Func("std::sys_common::backtrace::__rust_begin_short_backtrace<*>"),
       PrettyFrameGlob::Wildcard(0, 16),
       PrettyFrameGlob::Func("thread_trampoline")}));

  // clang-format on

  SetMatchers(std::move(matchers));
}

PrettyStackManager::Match PrettyStackManager::GetMatchAt(const Stack& stack,
                                                         size_t frame_index) const {
  for (const StackGlob& matcher : matchers_) {
    if (size_t match_count = StackGlobMatchesAt(matcher, stack, frame_index))
      return Match(match_count, matcher.description);
  }

  return Match();
}

std::vector<PrettyStackManager::FrameEntry> PrettyStackManager::ProcessStack(
    const Stack& stack) const {
  std::vector<FrameEntry> result;
  for (size_t stack_i = 0; stack_i < stack.size(); /* nothing */) {
    PrettyStackManager::Match match = GetMatchAt(stack, stack_i);

    if (match) {
      // Have a pretty stack entry, construct a FrameEntry for it.
      FrameEntry* entry = nullptr;
      if (!result.empty() && result.back().match &&
          result.back().match.description == match.description) {
        // This match is the same as the previous one, merge the two by appending our new frames to
        // the previous entry.
        entry = &result.back();
        entry->match.match_count += match.match_count;
      } else {
        // Got a new match, the frames go on a new entry.
        entry = &result.emplace_back();
        entry->begin_index = stack_i;
        entry->match = std::move(match);
      }
      for (size_t entry_i = 0; entry_i < match.match_count; entry_i++)
        entry->frames.push_back(stack[stack_i + entry_i]);
      stack_i += match.match_count;
    } else {
      // No match, append single stack entry.
      FrameEntry& entry = result.emplace_back();
      entry.begin_index = stack_i;
      entry.frames.push_back(stack[stack_i]);
      stack_i++;
    }
  }

  return result;
}

// static
size_t PrettyStackManager::StackGlobMatchesAt(const StackGlob& stack_glob, const Stack& stack,
                                              size_t frame_start_index) {
  if (frame_start_index + stack_glob.frames.size() > stack.size())
    return 0;  // Not enough room for all frame globs.

  size_t glob_index = 0;
  size_t stack_index = frame_start_index;

  // Number of wildcard positions left to possibly (but not necessarily) skip
  size_t wildcard_skip = 0;

  while (glob_index < stack_glob.frames.size() && stack_index < stack.size()) {
    const PrettyFrameGlob& frame_glob = stack_glob.frames[glob_index];
    const Frame* frame = stack[stack_index];

    if (frame_glob.is_wildcard()) {
      FX_DCHECK(!wildcard_skip);
      wildcard_skip = frame_glob.max_matches() - frame_glob.min_matches();
      // The min_matches will be eaten at the bottom of the loop.
    } else if (frame_glob.Matches(frame)) {
      wildcard_skip = 0;
    } else if (wildcard_skip == 0) {
      return 0;
    } else {
      wildcard_skip--;
      stack_index++;
      continue;
    }
    glob_index++;
    stack_index += frame_glob.min_matches();
  }

  if (stack_index > stack.size())
    return 0;  // Wildcard minimum is off the end of the stack.
  if (glob_index < stack_glob.frames.size())
    return 0;  // Not all frames required by the glob were matched.

  // Matched to the bottom of the stack.
  return stack_index - frame_start_index;
}

}  // namespace zxdb
