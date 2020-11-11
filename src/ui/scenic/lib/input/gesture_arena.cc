// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/gesture_arena.h"

#include <lib/syslog/cpp/macros.h>

#include <map>
#include <set>

#include "src/lib/fxl/macros.h"

namespace scenic_impl::input {

namespace {

bool IsYesType(GestureResponse response) {
  return response == GestureResponse::kYes || response == GestureResponse::kYesPrioritize;
}

bool IsHoldType(GestureResponse response) {
  return response == GestureResponse::kHold || response == GestureResponse::kHoldSuppress;
}

bool IsSuppressType(GestureResponse response) {
  return response == GestureResponse::kMaybeSuppress ||
         response == GestureResponse::kMaybePrioritizeSuppress ||
         response == GestureResponse::kHoldSuppress;
}

bool CanResolveMidContest(const std::map<GestureArena::Priority, GestureResponse>& response_map) {
  if (response_map.size() == 1) {
    return true;
  }

  for (const auto& [priority, response] : response_map) {
    if (IsYesType(response)) {
      // First Yes we reach triggers resolution.
      return true;
    }
    if (IsSuppressType(response)) {
      // Didn't reach any Yes.
      return false;
    }
  }

  // Didn't find any Yes.
  return false;
}

bool CanResolveAtSweep(const std::map<GestureArena::Priority, GestureResponse>& response_map) {
  if (response_map.size() == 1) {
    return true;
  }

  // If we don't find a Hold then resolution is possible.
  bool can_resolve = true;
  for (const auto& [priority, response] : response_map) {
    if (IsYesType(response)) {
      // First Yes we reach triggers resolution.
      return true;
    }
    if (IsHoldType(response)) {
      // Hold prevents resolution, unless we find a later Yes.
      can_resolve = false;
    }
    if (IsSuppressType(response)) {
      // Don't look at any further responses.
      break;
    }
  }

  return can_resolve;
}

// Compares two responses. Returns true if |high_priority| beats |low_priority|.
// The compared responses must never include any version of kUndefined, kHold or kNo, since none of
// those responses can win a contest unless they're the only contender.
bool WinsOver(GestureResponse high_priority, GestureResponse low_priority) {
  static_assert(GestureResponse::kYes == 0 && GestureResponse::kYesPrioritize == 1 &&
                GestureResponse::kMaybe == 2 && GestureResponse::kMaybePrioritize == 3 &&
                GestureResponse::kMaybeSuppress == 4 &&
                GestureResponse::kMaybePrioritizeSuppress == 5);

  // clang-format off
  static constexpr bool kComparison[6][6] {
    // Higher priority              Lower priority ->
    //  V            Yes,  YesP,  Maybe, MaybeP, MaybeS, MaybePS
    /* Yes */     { false, false, true,  true,   true,   true },
    /* YesP */    { true,  true,  true,  true,   true,   true },
    /* Maybe */   { false, false, false, false,  false,  false },
    /* MaybeP */  { false, false, true,  true,   true,   true },
    /* MaybeS */  { false, false, false, false,  false,  false },
    /* MaybePS */ { false, false, true,  true,   true,   true },
  };
  // clang-format on

  FX_DCHECK(high_priority >= 0 && high_priority < 6 && low_priority >= 0 && low_priority < 6);
  return kComparison[high_priority][low_priority];
}

// Determines the winner given a vector of responses ordered from highest to lowest priority.
ContenderId Resolve(const std::vector<std::pair<ContenderId, GestureResponse>>& responses) {
  FX_DCHECK(!responses.empty());
  if (responses.size() == 1) {
    return responses.front().first;
  }

  std::pair<ContenderId, GestureResponse> winner{kInvalidContenderId, GestureResponse::kUndefined};
  for (const auto& [id, response] : responses) {
    // Hold responses would have been suppressed and should be skipped.
    if (IsHoldType(response))
      continue;

    if (winner.first == kInvalidContenderId) {
      winner = {id, response};
      continue;
    }

    if (!WinsOver(winner.second, response))
      winner = {id, response};
  }

  FX_DCHECK(winner.first != kInvalidContenderId);
  return winner.first;
}

}  // namespace

GestureArena::GestureArena(std::vector<ContenderId> contenders) {
  FX_DCHECK(!contenders.empty());
  FX_DCHECK(std::set<ContenderId>(contenders.begin(), contenders.end()).size() == contenders.size())
      << "No duplicate contenders allowed";
  FX_DCHECK(std::count(contenders.begin(), contenders.end(), kInvalidContenderId) == 0)
      << "No contender can have id kInvalidContenderId";

  Priority priority = kInvalidPriority;
  for (const auto id : contenders) {
    const auto p = ++priority;
    FX_DCHECK(id != kInvalidContenderId);
    auto [it1, success1] = contenders_.emplace(id, Contender{.id = id, .priority = p});
    FX_DCHECK(success1);
    auto [it2, success2] = priority_to_id_.emplace(p, id);
    FX_DCHECK(success2);
    FX_DCHECK(priority_to_id_.at(it1->second.priority) == it1->second.id);
  }
}

void GestureArena::UpdateStream(uint64_t added_length, bool is_last_message) {
  FX_DCHECK(!stream_has_ended_);
  stream_length_ += added_length;
  if (is_last_message) {
    stream_has_ended_ = true;
  }
}

ContestResults GestureArena::RecordResponse(ContenderId contender_id,
                                            const std::vector<GestureResponse>& responses) {
  FX_DCHECK(std::count(responses.begin(), responses.end(), GestureResponse::kUndefined) == 0);
  FX_DCHECK(contenders_.count(contender_id));
  FX_DCHECK(!IsHoldType(contenders_.at(contender_id).response_status) || responses.size() == 1)
      << "Can only have a single response after kHold.";

  ContestResults results;
  for (const auto response : responses) {
    // Remove contender from the arena on a NO response.
    if (response == GestureResponse::kNo) {
      RemoveContender(contender_id);
      results.losers.push_back(contender_id);
      if (contenders_.empty()) {
        // No winners, but the contest is over.
        results.end_of_contest = true;
        break;
      }
    } else {
      AddResponse(contender_id, response);
    }

    const std::optional<ContenderId> winner = TryResolve();
    if (winner.has_value()) {
      results.winner = winner;
      contenders_.erase(winner.value());
      // Mark all remaining contenders as losers.
      for (const auto& [id, contender] : contenders_) {
        results.losers.emplace_back(id);
      }
      results.end_of_contest = true;
      break;
    }
  }

  return results;
}

void GestureArena::AddResponse(ContenderId contender_id, GestureResponse response) {
  FX_DCHECK(contenders_.count(contender_id) != 0);
  auto& contender = contenders_.at(contender_id);
  const Priority priority = contender.priority;

  // Find the next place in the queue that this contender hasn't placed a response, or the last one
  // if we're at stream end.
  size_t i = 0;
  while (i + index_of_current_responses_ < stream_length_ - 1 && i < responses_.size() &&
         responses_.at(i).count(priority) != 0) {
    ++i;
  }

  // Push events onto the end of this contender's queue.
  if (i == responses_.size()) {
    responses_.emplace_back();
  }
  FX_DCHECK(i < responses_.size());

  // Some DCHECKs to ensure the hold state is handled correctly.
  if (i + index_of_current_responses_ == stream_length_ - 1 &&
      responses_.at(i).count(priority) != 0) {
    FX_DCHECK(stream_has_ended_) << "Should only be able to receive extra responses on stream end.";
    FX_DCHECK(IsHoldType(responses_.at(i).at(priority)))
        << "Should only receive extra responses in the kHold phase.";
    FX_DCHECK(!IsHoldType(response)) << "Responses during kHold must not also be kHold";
  }

  responses_.at(i).insert_or_assign(priority, response);

  // Update current response status.
  FX_DCHECK(contender.response_status != GestureResponse::kNo);
  contender.response_status = response;
}

void GestureArena::RemoveContender(ContenderId contender_id) {
  // Remove all traces of the contender represented by |contender_id|.
  const auto priority = contenders_.at(contender_id).priority;
  const auto num_removed1 = contenders_.erase(contender_id);
  FX_DCHECK(num_removed1 == 1);
  const auto num_removed2 = priority_to_id_.erase(priority);
  FX_DCHECK(num_removed2 == 1);
  for (auto& response_map : responses_) {
    response_map.erase(priority);
  }
}

std::optional<ContenderId> GestureArena::TryResolve() {
  // Walk all complete sets of responses and try to resolve each response vector.
  while (!responses_.empty() && responses_.front().size() == contenders_.size()) {
    const auto& response_map = responses_.front();

    const bool at_sweep = stream_has_ended_ && index_of_current_responses_ == stream_length_ - 1;
    const bool can_resolve =
        at_sweep ? CanResolveAtSweep(response_map) : CanResolveMidContest(response_map);
    if (can_resolve) {
      std::vector<std::pair<ContenderId, GestureResponse>> ordered_responses;
      for (const auto& [priority, response] : response_map) {
        ordered_responses.emplace_back(priority_to_id_.at(priority), response);
      }
      return Resolve(ordered_responses);
    } else if (at_sweep) {
      // Don't drop the final set of responses if we're at sweep and still can't resolve.
      FX_DCHECK(responses_.size() == 1);
      break;
    }

    index_of_current_responses_++;
    responses_.pop_front();
  }

  return std::nullopt;
}

}  // namespace scenic_impl::input
