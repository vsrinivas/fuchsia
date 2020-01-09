// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/pretty_stack_manager.h"

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/mock_frame.h"
#include "src/developer/debug/zxdb/client/mock_stack_delegate.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

std::vector<std::unique_ptr<Frame>> GetTestFrames() {
  std::vector<std::unique_ptr<Frame>> frames;
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, 0x1001, 0x2001, "Function1",
                                               FileLine("file1.cc", 23)));
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, 0x1002, 0x2002, "Function2",
                                               FileLine("file2.cc", 23)));
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, 0x1003, 0x2003, "Function3",
                                               FileLine("file3.cc", 23)));
  frames.push_back(std::make_unique<MockFrame>(nullptr, nullptr, 0x1004, 0x2004, "Function4",
                                               FileLine("file4.cc", 23)));
  return frames;
}

}  // namespace

TEST(PrettyStackManager, StackGlobMatchesAt) {
  MockStackDelegate delegate;
  Stack stack(&delegate);
  delegate.set_stack(&stack);

  stack.SetFramesForTest(GetTestFrames(), true);

  // Single item that's not a match.
  PrettyStackManager::StackGlob single_unmatched("", {PrettyFrameGlob::Func("Unmatched")});
  EXPECT_EQ(0u, PrettyStackManager::StackGlobMatchesAt(single_unmatched, stack, 0));
  EXPECT_EQ(0u, PrettyStackManager::StackGlobMatchesAt(single_unmatched, stack, 1));
  EXPECT_EQ(0u, PrettyStackManager::StackGlobMatchesAt(single_unmatched, stack, 2));

  // Single function that is a match.
  PrettyStackManager::StackGlob single_matched_func("", {PrettyFrameGlob::Func("Function2")});
  EXPECT_EQ(1u, PrettyStackManager::StackGlobMatchesAt(single_matched_func, stack, 1));

  // Single file that is a match.
  PrettyStackManager::StackGlob single_matched_file("", {PrettyFrameGlob::File("file2.cc")});
  EXPECT_EQ(1u, PrettyStackManager::StackGlobMatchesAt(single_matched_file, stack, 1));

  // Function match followed by file match.
  PrettyStackManager::StackGlob double_match(
      "", {PrettyFrameGlob::Func("Function2"), PrettyFrameGlob::File("file3.cc")});
  EXPECT_EQ(0u, PrettyStackManager::StackGlobMatchesAt(double_match, stack, 0));
  EXPECT_EQ(2u, PrettyStackManager::StackGlobMatchesAt(double_match, stack, 1));

  // Wildcard that matches one thing at the beginning
  PrettyStackManager::StackGlob single_wildcard_beginning(
      "", {PrettyFrameGlob::Wildcard(1, 1), PrettyFrameGlob::File("file2.cc")});
  EXPECT_EQ(2u, PrettyStackManager::StackGlobMatchesAt(single_wildcard_beginning, stack, 0));
  EXPECT_EQ(0u, PrettyStackManager::StackGlobMatchesAt(single_wildcard_beginning, stack, 1));

  // Wildcard that matches exactly two things in the middle.
  PrettyStackManager::StackGlob double_wildcard_middle(
      "", {PrettyFrameGlob::File("file1.cc"), PrettyFrameGlob::Wildcard(2, 2),
           PrettyFrameGlob::Func("Function4")});
  EXPECT_EQ(4u, PrettyStackManager::StackGlobMatchesAt(double_wildcard_middle, stack, 0));
  EXPECT_EQ(0u, PrettyStackManager::StackGlobMatchesAt(double_wildcard_middle, stack, 1));

  // Wildcard that doesn't match.
  PrettyStackManager::StackGlob wildcard_miss(
      "", {PrettyFrameGlob::File("file1.cc"), PrettyFrameGlob::Wildcard(1, 1),
           PrettyFrameGlob::Func("Function4")});
  EXPECT_EQ(0u, PrettyStackManager::StackGlobMatchesAt(wildcard_miss, stack, 0));

  // Wildcard that matches anything at the end. Since the wildcard matches as few items as possible,
  // it should match the minimum range when the wildcard is at the end of the glob list (in this
  // case 1 frame, giving 2 total frames matched).
  PrettyStackManager::StackGlob wildcard_end(
      "", {PrettyFrameGlob::File("file2.cc"), PrettyFrameGlob::Wildcard(1, 3)});
  EXPECT_EQ(0u, PrettyStackManager::StackGlobMatchesAt(wildcard_end, stack, 0));
  EXPECT_EQ(2u, PrettyStackManager::StackGlobMatchesAt(wildcard_end, stack, 1));

  // Wildcard runs off the end.
  PrettyStackManager::StackGlob wildcard_off_end(
      "", {PrettyFrameGlob::File("file2.cc"), PrettyFrameGlob::Wildcard(4, 4)});
  EXPECT_EQ(0u, PrettyStackManager::StackGlobMatchesAt(wildcard_off_end, stack, 1));

  // Wildcard requires too few items (but would otherwise match).
  PrettyStackManager::StackGlob wildcard_too_many(
      "", {PrettyFrameGlob::File("file1.cc"), PrettyFrameGlob::Wildcard(1, 1),
           PrettyFrameGlob::File("file4.cc")});
  EXPECT_EQ(0u, PrettyStackManager::StackGlobMatchesAt(wildcard_too_many, stack, 1));
}

TEST(PrettyStackManager, GetMatchAt) {
  MockStackDelegate delegate;
  Stack stack(&delegate);
  delegate.set_stack(&stack);

  stack.SetFramesForTest(GetTestFrames(), true);

  auto manager = fxl::MakeRefCounted<PrettyStackManager>();

  // Empty matchers should never match.
  EXPECT_FALSE(manager->GetMatchAt(stack, 0));
  EXPECT_FALSE(manager->GetMatchAt(stack, 1));
  EXPECT_FALSE(manager->GetMatchAt(stack, 2));
  EXPECT_FALSE(manager->GetMatchAt(stack, 3));

  // Supply two non-overlapping matchers.
  std::vector<PrettyStackManager::StackGlob> matchers;
  matchers.push_back(
      PrettyStackManager::StackGlob("Function1 Matcher", {PrettyFrameGlob::Func("Function1")}));
  matchers.push_back(
      PrettyStackManager::StackGlob("file2 Matcher", {PrettyFrameGlob::File("file2.cc")}));
  manager->SetMatchers(matchers);

  PrettyStackManager::Match result = manager->GetMatchAt(stack, 0);
  EXPECT_TRUE(result);
  EXPECT_EQ(1u, result.match_count);
  EXPECT_EQ("Function1 Matcher", result.description);

  result = manager->GetMatchAt(stack, 1);
  EXPECT_TRUE(result);
  EXPECT_EQ(1u, result.match_count);
  EXPECT_EQ("file2 Matcher", result.description);

  // Now add on top of that a wildcard match that covers more range, it should now be returned in
  // both cases since its longer.
  matchers.push_back(PrettyStackManager::StackGlob(
      "Star Matcher", {PrettyFrameGlob::Wildcard(1, 2), PrettyFrameGlob::File("file3.cc")}));
  manager->SetMatchers(matchers);

  result = manager->GetMatchAt(stack, 0);
  EXPECT_TRUE(result);
  EXPECT_EQ(3u, result.match_count);
  EXPECT_EQ("Star Matcher", result.description);

  result = manager->GetMatchAt(stack, 1);
  EXPECT_TRUE(result);
  EXPECT_EQ(2u, result.match_count);
  EXPECT_EQ("Star Matcher", result.description);
}

}  // namespace zxdb
