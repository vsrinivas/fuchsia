// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/pretty_stack_manager.h"

#include <algorithm>

#include "src/developer/debug/zxdb/client/stack.h"

namespace zxdb {

void PrettyStackManager::SetMatchers(std::vector<StackGlob> matchers) {
  matchers_ = std::move(matchers);
  std::sort(matchers_.begin(), matchers_.end(), [](const auto& left, const auto& right) {
    return left.frames.size() > right.frames.size();
  });
}

PrettyStackManager::Match PrettyStackManager::GetMatchAt(const Stack* stack,
                                                         size_t frame_index) const {
  for (const StackGlob& matcher : matchers_) {
    if (size_t match_count = StackGlobMatchesAt(matcher, stack, frame_index))
      return Match(match_count, matcher.description);
  }

  return Match();
}

// static
size_t PrettyStackManager::StackGlobMatchesAt(const StackGlob& stack_glob, const Stack* stack,
                                              size_t frame_start_index) {
  if (frame_start_index + stack_glob.frames.size() >= stack->size())
    return 0;  // Not enough room for all frame globs.

  size_t glob_index = 0;
  size_t stack_index = frame_start_index;

  // Number of wildcard positions left to possibly (not not necessarily) skip
  size_t wildcard_skip = 0;

  while (glob_index < stack_glob.frames.size() && stack_index < stack->size()) {
    const PrettyFrameGlob& frame_glob = stack_glob.frames[glob_index];
    const Frame* frame = (*stack)[stack_index];

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

  if (stack_index > stack->size())
    return 0;  // Wildcard minimum is off the end of the stack.

  // Matched to the bottom of the stack.
  return stack_index - frame_start_index;
}

}  // namespace zxdb
