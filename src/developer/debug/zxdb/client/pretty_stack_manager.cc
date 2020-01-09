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

  // TODO(brettw) Add Rust async loop waiting (needs more powerful matching).

  // C startup code (although it is only one frame, it hides several lines of complex useless
  // parameters in the "backtrace" view).
  PrettyFrameGlob libc_start_main = PrettyFrameGlob::FuncFile("start_main", "__libc_start_main.c");
  matchers.push_back(StackGlob("libc startup", {libc_start_main}));

  // Rust startup code.
  // TODO(brettw) replace some of the wildcards with more precise matching when it's possible to
  // express the varying template arguments and names in the matchers.
  matchers.push_back(StackGlob("Rust startup",
                               {PrettyFrameGlob::Wildcard(2, 2),  // lambda, try
                                PrettyFrameGlob::Func("__rust_maybe_catch_panic"),
                                PrettyFrameGlob::Wildcard(2, 2),  // lang_start_internal, lang_start
                                PrettyFrameGlob::Func("main"), libc_start_main}));

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
    FrameEntry& entry = result.emplace_back();
    entry.begin_index = stack_i;
    entry.match = GetMatchAt(stack, stack_i);

    if (entry.match) {
      for (size_t entry_i = 0; entry_i < entry.match.match_count; entry_i++)
        entry.frames.push_back(stack[stack_i + entry_i]);
      stack_i += entry.match.match_count;
    } else {
      // No match, append single stack entry.
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

  // Number of wildcard positions left to possibly (not not necessarily) skip
  size_t wildcard_skip = 0;

  while (glob_index < stack_glob.frames.size() && stack_index < stack.size()) {
    const PrettyFrameGlob& frame_glob = stack_glob.frames[glob_index];
    const Frame* frame = stack[stack_index];

    if (frame_glob.is_wildcard()) {
      FXL_DCHECK(!wildcard_skip);
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

  // Matched to the bottom of the stack.
  return stack_index - frame_start_index;
}

}  // namespace zxdb
