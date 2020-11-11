// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/gesture_arena.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace lib_ui_input_tests {
namespace {

using scenic_impl::input::ContenderId;
using scenic_impl::input::ContestResults;
using scenic_impl::input::GestureArena;
using scenic_impl::input::GestureResponse;

struct Stream {
  uint64_t length = 0;
  bool is_last_message = false;
};

struct Response {
  ContenderId contender_id = scenic_impl::input::kInvalidContenderId;
  std::vector<GestureResponse> responses;
};

struct Update {
  Response response;
  ContestResults result;
};

struct Contest {
  std::vector<ContenderId> contenders_high_to_low;
  Stream stream;
  std::vector<Update> updates;
};

Contest SingleContender_ShouldWinWithYes() {
  return {
      .contenders_high_to_low = {1},
      .stream = {.length = 1, .is_last_message = false},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kYes}},
                  .result = {.winner = 1, .losers = {}, .end_of_contest = true},
              },
          },
  };
}

Contest SingleContender_ShouldWinWithMaybe() {
  return {
      .contenders_high_to_low = {1},
      .stream = {.length = 1, .is_last_message = false},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kMaybe}},
                  .result = {.winner = 1, .losers = {}, .end_of_contest = true},
              },
          },
  };
}

Contest SingleContender_ShouldWinWithHold() {
  return {
      .contenders_high_to_low = {1},
      .stream = {.length = 1, .is_last_message = false},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kHold}},
                  .result = {.winner = 1, .losers = {}, .end_of_contest = true},
              },
          },
  };
}

Contest SingleContender_ShouldLoseWithNo() {
  return {
      .contenders_high_to_low = {1},
      .stream = {.length = 1, .is_last_message = false},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kNo}},
                  .result = {.losers = {1}, .end_of_contest = true},
              },
          },
  };
}

Contest SingleContender_ShouldWinWithYesFollowdByNo() {
  return {
      .contenders_high_to_low = {1},
      .stream = {.length = 2, .is_last_message = false},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1,
                               .responses = {GestureResponse::kYes, GestureResponse::kNo}},
                  .result = {.winner = 1, .losers = {}, .end_of_contest = true},
              },
          },
  };
}

Contest SingleContender_ShouldLoseWithNoFollowdByYes() {
  return {
      .contenders_high_to_low = {1},
      .stream = {.length = 2, .is_last_message = false},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1,
                               .responses = {GestureResponse::kNo, GestureResponse::kYes}},
                  .result = {.losers = {1}, .end_of_contest = true},
              },
          },
  };
}

Contest MultipleContenders_LowestPriorityShouldWin_IfBothAnswerYes() {
  return {
      .contenders_high_to_low = {1, 2},  // 1 has higher priority.
      .stream = {.length = 1, .is_last_message = false},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kYes}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2, .responses = {GestureResponse::kYes}},
                  .result = {.winner = 2, .losers = {1}, .end_of_contest = true},
              },
          },
  };
}

// Same as previous test, with priorities reversed. To confirm that response order doesn't matter.
Contest MultipleContenders_LowestPriorityShouldWin_IfBothAnswerYes_ReversedPriority() {
  return {
      .contenders_high_to_low = {2, 1},  // 2 has higher priority.
      .stream = {.length = 1, .is_last_message = false},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kYes}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2, .responses = {GestureResponse::kYes}},
                  .result = {.winner = 1, .losers = {2}, .end_of_contest = true},
              },
          },
  };
}

Contest MultipleContenders_HighestPriorityYesPrioritize_ShouldWin() {
  return {
      .contenders_high_to_low = {1, 2, 3, 4},
      .stream = {.length = 1, .is_last_message = false},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kYes}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2, .responses = {GestureResponse::kYesPrioritize}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 3, .responses = {GestureResponse::kYesPrioritize}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 4, .responses = {GestureResponse::kYes}},
                  .result = {.winner = 2, .losers = {1, 3, 4}, .end_of_contest = true},
              },
          },
  };
}

// If all contenders respond Maybe, there should be no resolution until responses for the entire
// stream have been received from every contender.
Contest AllMaybeShouldPreventResolution_UntilSweep() {
  return {
      .contenders_high_to_low = {1, 2},
      .stream = {.length = 2, .is_last_message = true},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kMaybe}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2, .responses = {GestureResponse::kMaybe}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kMaybe}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2, .responses = {GestureResponse::kMaybe}},
                  .result = {.winner = 2, .losers = {1}, .end_of_contest = true},
              },
          },
  };
}

Contest HigherPriorityHold_AgainstMaybeAtSweep_ShouldPreventResolution() {
  return {
      .contenders_high_to_low = {1, 2},
      .stream = {.length = 1, .is_last_message = true},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kHold}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2, .responses = {GestureResponse::kMaybe}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kNo}},
                  .result = {.winner = 2, .losers = {1}, .end_of_contest = true},
              },
          },
  };
}

Contest LowerPriorityHold_AgainstMaybeAtSweep_ShouldPreventResolution() {
  return {
      .contenders_high_to_low = {2, 1},
      .stream = {.length = 1, .is_last_message = true},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kHold}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2, .responses = {GestureResponse::kMaybe}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kNo}},
                  .result = {.winner = 2, .losers = {1}, .end_of_contest = true},
              },
          },
  };
}

Contest HigherPriorityHold_AgainstMaybeSuppressAtSweep_ShouldPreventResolution() {
  return {
      .contenders_high_to_low = {1, 2},
      .stream = {.length = 1, .is_last_message = true},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kHold}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2, .responses = {GestureResponse::kMaybeSuppress}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kNo}},
                  .result = {.winner = 2, .losers = {1}, .end_of_contest = true},
              },
          },
  };
}

Contest LowerPriorityHold_AgainstMaybeSuppressAtSweep_ShouldNotPreventResolution() {
  return {
      .contenders_high_to_low = {2, 1},
      .stream = {.length = 1, .is_last_message = true},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kHold}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2, .responses = {GestureResponse::kMaybeSuppress}},
                  .result = {.winner = 2, .losers = {1}, .end_of_contest = true},
              },
          },
  };
}

Contest HoldFollowedByMaybe_InTheSameVector_ShouldResolve() {
  return {
      .contenders_high_to_low = {1, 2},
      .stream = {.length = 2, .is_last_message = true},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1,
                               .responses = {GestureResponse::kMaybe, GestureResponse::kMaybe}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2,
                               .responses = {GestureResponse::kHold, GestureResponse::kHold,
                                             GestureResponse::kMaybe}},
                  .result = {.winner = 2, .losers = {1}, .end_of_contest = true},
              },
          },
  };
}

Contest MultipleHold_ShouldResolve_WhenAllHaveBeenReleased() {
  return {
      .contenders_high_to_low = {1, 2, 3},
      .stream = {.length = 1, .is_last_message = true},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kHold}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2, .responses = {GestureResponse::kMaybe}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 3, .responses = {GestureResponse::kHold}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 3, .responses = {GestureResponse::kMaybe}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kYes}},
                  .result = {.winner = 1, .losers = {2, 3}, .end_of_contest = true},
              },
          },
  };
}

Contest Hold_ReleasedAheadOfTime_ShouldResolve() {
  return {
      .contenders_high_to_low = {1, 2},
      .stream = {.length = 2, .is_last_message = true},
      .updates =
          std::vector<Update>{
              {
                  .response = {.contender_id = 1,
                               .responses = {GestureResponse::kHoldSuppress,
                                             GestureResponse::kHoldSuppress}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 1, .responses = {GestureResponse::kYesPrioritize}},
                  .result = {.end_of_contest = false},
              },
              {
                  .response = {.contender_id = 2,
                               .responses = {GestureResponse::kYes, GestureResponse::kYes}},
                  .result = {.winner = 1, .losers = {2}, .end_of_contest = true},
              },
          },
  };
}

class GestureArenaParameterizedTest : public testing::TestWithParam<Contest> {};
INSTANTIATE_TEST_CASE_P(
    /*no prefix*/, GestureArenaParameterizedTest,
    testing::Values(
        SingleContender_ShouldWinWithYes(),                                             // 0
        SingleContender_ShouldWinWithMaybe(),                                           // 1
        SingleContender_ShouldWinWithHold(),                                            // 2
        SingleContender_ShouldLoseWithNo(),                                             // 3
        SingleContender_ShouldWinWithYesFollowdByNo(),                                  // 4
        SingleContender_ShouldLoseWithNoFollowdByYes(),                                 // 5
        MultipleContenders_LowestPriorityShouldWin_IfBothAnswerYes(),                   // 6
        MultipleContenders_LowestPriorityShouldWin_IfBothAnswerYes_ReversedPriority(),  // 7
        MultipleContenders_HighestPriorityYesPrioritize_ShouldWin(),                    // 8
        AllMaybeShouldPreventResolution_UntilSweep(),                                   // 9
        HigherPriorityHold_AgainstMaybeAtSweep_ShouldPreventResolution(),               // 10
        LowerPriorityHold_AgainstMaybeAtSweep_ShouldPreventResolution(),                // 11
        HigherPriorityHold_AgainstMaybeSuppressAtSweep_ShouldPreventResolution(),       // 12
        LowerPriorityHold_AgainstMaybeSuppressAtSweep_ShouldNotPreventResolution(),     // 13
        HoldFollowedByMaybe_InTheSameVector_ShouldResolve(),                            // 14
        MultipleHold_ShouldResolve_WhenAllHaveBeenReleased(),                           // 15
        Hold_ReleasedAheadOfTime_ShouldResolve()                                        // 16
        ));
TEST_P(GestureArenaParameterizedTest, Basic) {
  const Contest contest = GetParam();
  GestureArena arena(contest.contenders_high_to_low);
  arena.UpdateStream(contest.stream.length, contest.stream.is_last_message);

  uint32_t update_count = 0;
  for (const auto& update : contest.updates) {
    ++update_count;
    const auto& response = update.response;
    const auto& expected_result = update.result;
    const auto result = arena.RecordResponse(response.contender_id, response.responses);
    EXPECT_EQ(result.end_of_contest, expected_result.end_of_contest)
        << "Failed on update " << update_count;
    EXPECT_EQ(result.winner.has_value(), expected_result.winner.has_value())
        << "Failed on update " << update_count;
    if (result.winner && expected_result.winner) {
      EXPECT_EQ(result.winner.value(), expected_result.winner.value())
          << "Failed on update " << update_count;
    }

    EXPECT_THAT(result.losers, testing::UnorderedElementsAreArray(expected_result.losers))
        << "Failed on update " << update_count;
  }
}

// Test that checks that all pairs of responses correctly interact with priority at sweep.
enum class Win { kLeft, kRight, kHold, kNoWinner };
class GestureArenaResponsePairTest
    : public testing::TestWithParam<std::tuple<GestureResponse, GestureResponse, Win>> {};
INSTANTIATE_TEST_CASE_P(
    /*no header*/, GestureArenaResponsePairTest,
    testing::Values(
        // clang-format off
        std::make_tuple(GestureResponse::kYes, GestureResponse::kYes, Win::kRight), // 0
        std::make_tuple(GestureResponse::kYes, GestureResponse::kYesPrioritize, Win::kRight), // 1
        std::make_tuple(GestureResponse::kYes, GestureResponse::kMaybe, Win::kLeft), // 2
        std::make_tuple(GestureResponse::kYes, GestureResponse::kMaybePrioritize, Win::kLeft), // 3
        std::make_tuple(GestureResponse::kYes, GestureResponse::kMaybeSuppress, Win::kLeft), // 4
        std::make_tuple(GestureResponse::kYes, GestureResponse::kMaybePrioritizeSuppress, Win::kLeft), // 5
        std::make_tuple(GestureResponse::kYes, GestureResponse::kHold, Win::kLeft), // 6
        std::make_tuple(GestureResponse::kYes, GestureResponse::kHoldSuppress, Win::kLeft), // 7
        std::make_tuple(GestureResponse::kYes, GestureResponse::kNo, Win::kLeft), // 8

        std::make_tuple(GestureResponse::kYesPrioritize, GestureResponse::kYes, Win::kLeft), // 9
        std::make_tuple(GestureResponse::kYesPrioritize, GestureResponse::kYesPrioritize, Win::kLeft), // 10
        std::make_tuple(GestureResponse::kYesPrioritize, GestureResponse::kMaybe, Win::kLeft), // 11
        std::make_tuple(GestureResponse::kYesPrioritize, GestureResponse::kMaybePrioritize, Win::kLeft), // 12
        std::make_tuple(GestureResponse::kYesPrioritize, GestureResponse::kMaybeSuppress, Win::kLeft), // 13
        std::make_tuple(GestureResponse::kYesPrioritize, GestureResponse::kMaybePrioritizeSuppress, Win::kLeft), // 14
        std::make_tuple(GestureResponse::kYesPrioritize, GestureResponse::kHold, Win::kLeft), // 15
        std::make_tuple(GestureResponse::kYesPrioritize, GestureResponse::kHoldSuppress, Win::kLeft), // 16
        std::make_tuple(GestureResponse::kYesPrioritize, GestureResponse::kNo, Win::kLeft), // 17
        
        std::make_tuple(GestureResponse::kMaybe, GestureResponse::kYes, Win::kRight), // 18
        std::make_tuple(GestureResponse::kMaybe, GestureResponse::kYesPrioritize, Win::kRight), // 19
        std::make_tuple(GestureResponse::kMaybe, GestureResponse::kMaybe, Win::kRight), // 20
        std::make_tuple(GestureResponse::kMaybe, GestureResponse::kMaybePrioritize, Win::kRight), // 21
        std::make_tuple(GestureResponse::kMaybe, GestureResponse::kMaybeSuppress, Win::kRight), // 22
        std::make_tuple(GestureResponse::kMaybe, GestureResponse::kMaybePrioritizeSuppress, Win::kRight), // 23
        std::make_tuple(GestureResponse::kMaybe, GestureResponse::kHold, Win::kHold), // 24
        std::make_tuple(GestureResponse::kMaybe, GestureResponse::kHoldSuppress, Win::kHold), // 25
        std::make_tuple(GestureResponse::kMaybe, GestureResponse::kNo, Win::kLeft), // 26

        std::make_tuple(GestureResponse::kMaybePrioritize, GestureResponse::kYes, Win::kRight), // 27
        std::make_tuple(GestureResponse::kMaybePrioritize, GestureResponse::kYesPrioritize, Win::kRight), // 28
        std::make_tuple(GestureResponse::kMaybePrioritize, GestureResponse::kMaybe, Win::kLeft), // 29
        std::make_tuple(GestureResponse::kMaybePrioritize, GestureResponse::kMaybeSuppress, Win::kLeft), // 30
        std::make_tuple(GestureResponse::kMaybePrioritize, GestureResponse::kMaybePrioritize, Win::kLeft), // 31
        std::make_tuple(GestureResponse::kMaybePrioritize, GestureResponse::kMaybePrioritizeSuppress, Win::kLeft), // 32
        std::make_tuple(GestureResponse::kMaybePrioritize, GestureResponse::kHold, Win::kHold), // 33
        std::make_tuple(GestureResponse::kMaybePrioritize, GestureResponse::kHoldSuppress, Win::kHold), // 34
        std::make_tuple(GestureResponse::kMaybePrioritize, GestureResponse::kNo, Win::kLeft), // 35

        std::make_tuple(GestureResponse::kMaybeSuppress, GestureResponse::kYes, Win::kRight), // 36
        std::make_tuple(GestureResponse::kMaybeSuppress, GestureResponse::kYesPrioritize, Win::kRight), // 37
        std::make_tuple(GestureResponse::kMaybeSuppress, GestureResponse::kMaybe, Win::kRight), // 38
        std::make_tuple(GestureResponse::kMaybeSuppress, GestureResponse::kMaybePrioritize, Win::kRight), // 39
        std::make_tuple(GestureResponse::kMaybeSuppress, GestureResponse::kMaybeSuppress, Win::kRight), // 40
        std::make_tuple(GestureResponse::kMaybeSuppress, GestureResponse::kMaybePrioritizeSuppress, Win::kRight), // 41
        std::make_tuple(GestureResponse::kMaybeSuppress, GestureResponse::kHold, Win::kLeft), // 42
        std::make_tuple(GestureResponse::kMaybeSuppress, GestureResponse::kHoldSuppress, Win::kLeft), // 43
        std::make_tuple(GestureResponse::kMaybeSuppress, GestureResponse::kNo, Win::kLeft), // 44

        std::make_tuple(GestureResponse::kMaybePrioritizeSuppress, GestureResponse::kYes, Win::kRight), // 45
        std::make_tuple(GestureResponse::kMaybePrioritizeSuppress, GestureResponse::kYesPrioritize, Win::kRight), // 46
        std::make_tuple(GestureResponse::kMaybePrioritizeSuppress, GestureResponse::kMaybe, Win::kLeft), // 47
        std::make_tuple(GestureResponse::kMaybePrioritizeSuppress, GestureResponse::kMaybePrioritize, Win::kLeft), // 48
        std::make_tuple(GestureResponse::kMaybePrioritizeSuppress, GestureResponse::kMaybeSuppress, Win::kLeft), // 49
        std::make_tuple(GestureResponse::kMaybePrioritizeSuppress, GestureResponse::kMaybePrioritizeSuppress, Win::kLeft), // 50
        std::make_tuple(GestureResponse::kMaybePrioritizeSuppress, GestureResponse::kHold, Win::kLeft), // 51
        std::make_tuple(GestureResponse::kMaybePrioritizeSuppress, GestureResponse::kHoldSuppress, Win::kLeft), // 52
        std::make_tuple(GestureResponse::kMaybePrioritizeSuppress, GestureResponse::kNo, Win::kLeft), // 53

        std::make_tuple(GestureResponse::kHold, GestureResponse::kYes, Win::kRight), // 54
        std::make_tuple(GestureResponse::kHold, GestureResponse::kYesPrioritize, Win::kRight), // 55
        std::make_tuple(GestureResponse::kHold, GestureResponse::kMaybe, Win::kHold), // 56
        std::make_tuple(GestureResponse::kHold, GestureResponse::kMaybePrioritize, Win::kHold), // 57
        std::make_tuple(GestureResponse::kHold, GestureResponse::kMaybeSuppress, Win::kHold), // 58
        std::make_tuple(GestureResponse::kHold, GestureResponse::kMaybePrioritizeSuppress, Win::kHold), // 59
        std::make_tuple(GestureResponse::kHold, GestureResponse::kHold, Win::kHold), // 60
        std::make_tuple(GestureResponse::kHold, GestureResponse::kHoldSuppress, Win::kHold), // 61
        std::make_tuple(GestureResponse::kHold, GestureResponse::kNo, Win::kLeft), // 62

        std::make_tuple(GestureResponse::kHoldSuppress, GestureResponse::kYes, Win::kHold), // 63
        std::make_tuple(GestureResponse::kHoldSuppress, GestureResponse::kYesPrioritize, Win::kHold), // 64
        std::make_tuple(GestureResponse::kHoldSuppress, GestureResponse::kMaybe, Win::kHold), // 65
        std::make_tuple(GestureResponse::kHoldSuppress, GestureResponse::kMaybePrioritize, Win::kHold), // 66
        std::make_tuple(GestureResponse::kHoldSuppress, GestureResponse::kMaybeSuppress, Win::kHold), // 67
        std::make_tuple(GestureResponse::kHoldSuppress, GestureResponse::kMaybePrioritizeSuppress, Win::kHold), // 68
        std::make_tuple(GestureResponse::kHoldSuppress, GestureResponse::kHold, Win::kHold), // 69
        std::make_tuple(GestureResponse::kHoldSuppress, GestureResponse::kHoldSuppress, Win::kHold), // 70
        std::make_tuple(GestureResponse::kHoldSuppress, GestureResponse::kNo, Win::kLeft), // 71

        std::make_tuple(GestureResponse::kNo, GestureResponse::kYes, Win::kRight), // 72
        std::make_tuple(GestureResponse::kNo, GestureResponse::kYesPrioritize, Win::kRight), // 73
        std::make_tuple(GestureResponse::kNo, GestureResponse::kMaybe, Win::kRight), // 74
        std::make_tuple(GestureResponse::kNo, GestureResponse::kMaybePrioritize, Win::kRight), // 75
        std::make_tuple(GestureResponse::kNo, GestureResponse::kMaybeSuppress, Win::kRight), // 76
        std::make_tuple(GestureResponse::kNo, GestureResponse::kMaybePrioritizeSuppress, Win::kRight), // 77
        std::make_tuple(GestureResponse::kNo, GestureResponse::kHold, Win::kRight), // 78
        std::make_tuple(GestureResponse::kNo, GestureResponse::kHoldSuppress, Win::kRight), // 79
        std::make_tuple(GestureResponse::kNo, GestureResponse::kNo, Win::kNoWinner) // 80
        // clang-format on
        ));
TEST_P(GestureArenaResponsePairTest, PairWiseComparisonTest) {
  constexpr ContenderId kId1 = 1, kId2 = 2;
  const auto [response1, response2, expected_winner] = GetParam();
  GestureArena arena(/*contenders*/ {kId1, kId2});
  arena.UpdateStream(1, /*is_last_message*/ true);

  arena.RecordResponse(kId1, {response1});
  const auto result = arena.RecordResponse(kId2, {response2});

  switch (expected_winner) {
    case Win::kLeft:
      EXPECT_TRUE(result.end_of_contest);
      ASSERT_TRUE(result.winner.has_value());
      EXPECT_EQ(result.winner.value(), kId1);
      break;
    case Win::kRight:
      EXPECT_TRUE(result.end_of_contest);
      ASSERT_TRUE(result.winner.has_value());
      EXPECT_EQ(result.winner.value(), kId2);
      break;
    case Win::kHold:
      EXPECT_FALSE(result.end_of_contest);
      EXPECT_FALSE(result.winner.has_value());
      EXPECT_TRUE(result.losers.empty());
      break;
    case Win::kNoWinner:
      EXPECT_TRUE(result.end_of_contest);
      EXPECT_FALSE(result.winner.has_value());
      break;
    default:
      EXPECT_TRUE(false);
      break;
  }
}

}  // namespace
}  // namespace lib_ui_input_tests
