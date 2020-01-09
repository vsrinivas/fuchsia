// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PRETTY_STACK_MANAGER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PRETTY_STACK_MANAGER_H_

#include <vector>

#include "gtest/gtest_prod.h"
#include "src/developer/debug/zxdb/client/pretty_frame_glob.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class Stack;

// Matches sequences of frames for pretty-ifying stacks. The patterns are expressed in
// PrettyFrameGlobs which match different parts of the stack frame or a wildcard range. The matched
// frames can then be collapsed and named with a descriptive string.
//
// Wildcard matching matches as few stack entries as possible (unlike many regular expression
// systems). Recursion can make the same sequence of frames appear multiple times in a stack, and
// we always want to hide as few frames as possible.
//
// Wildcard matching does not do backtracking. This means that the first frame after a wildcard
// picks up matching the stack again. If the sequence of matchers after this doesn't match, the
// code won't go search for a possibly different interpretation of the wildcard that does match.
// This behavior is unnecessary given typical stack matching requirements and affects complexity and
// performance.
class PrettyStackManager : public fxl::RefCountedThreadSafe<PrettyStackManager> {
 public:
  // Construct with fxl::MakeRefCounted().

  struct StackGlob {
    StackGlob(std::string d, std::vector<PrettyFrameGlob> f)
        : description(std::move(d)), frames(std::move(f)) {}

    std::string description;
    std::vector<PrettyFrameGlob> frames;
  };

  struct Match {
    Match() = default;
    Match(size_t mc, const std::string& d) : match_count(mc), description(d) {}

    // Returns true if this matches any frames.
    operator bool() const { return match_count > 0; }

    size_t match_count = 0;
    std::string description;
  };

  // The grouped results of prettifying an entire stack.
  struct FrameEntry {
    // Index into the stack fo the first frame.
    size_t begin_index = 0;

    // Match if there was one starting at this location.
    Match match;

    // The frames corresponding to this item. If there's a match, this will contain the range of
    // frames identified by the match_count. If there's no match, this will contain one frame.
    std::vector<const Frame*> frames;
  };

  void SetMatchers(std::vector<StackGlob> matchers);

  // Loads the hardcoded default matchers.
  //
  // TODO(bug 43549) this should be loaded from a configuration file somehow associated with the
  // user's build instead of being hardcoded. This function can then be deleted.
  void LoadDefaultMatchers();

  // Returns the best match at the given index. The result will match no frames if there was no
  // match at the given index.
  Match GetMatchAt(const Stack& stack, size_t frame_index) const;

  // Processes an entire stack, returning the grouped results.
  std::vector<FrameEntry> ProcessStack(const Stack& stack) const;

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(PrettyStackManager);
  FRIEND_MAKE_REF_COUNTED(PrettyStackManager);

  FRIEND_TEST(PrettyStackManager, StackGlobMatchesAt);

  PrettyStackManager() = default;

  // Returns the number of frames matched (may be > stack_glob.size() if there were wildcard
  // matches). Will return 0 if it's not a match.
  static size_t StackGlobMatchesAt(const StackGlob& stack_glob, const Stack& stack,
                                   size_t frame_start_index);

  // Sorted in order of decreasing size (longest matchers are first).
  std::vector<StackGlob> matchers_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_PRETTY_STACK_MANAGER_H_
