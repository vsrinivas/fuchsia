// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/gesture_arena.h"

#include <lib/syslog/cpp/macros.h>

#include <set>

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

// Find the next place in the queue where contender with |priority| hasn't placed a response.
size_t FindResponseIndex(
    const std::deque<std::map<GestureArena::Priority, GestureResponse>>& response_queue,
    const GestureArena::Priority priority, const bool queue_is_full) {
  size_t i = 0;
  while (i < response_queue.size() && response_queue.at(i).count(priority) != 0) {
    ++i;
  }

  // When the queue is full we want to replace the last value instead of extending the queue.
  if (queue_is_full && i == response_queue.size()) {
    --i;
  }

  return i;
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

  if (contenders.size() == 1) {
    contest_has_ended_ = true;
  }
}

void GestureArena::UpdateStream(uint64_t new_message_count, bool is_last_message) {
  FX_DCHECK(!stream_has_ended_);
  response_queue_expected_size_ += new_message_count;
  stream_has_ended_ = is_last_message;
}

ContestResults GestureArena::RecordResponses(ContenderId contender_id,
                                             const std::vector<GestureResponse>& responses) {
  FX_DCHECK(!contest_has_ended_);
  FX_DCHECK(std::count(responses.begin(), responses.end(), GestureResponse::kUndefined) == 0);
  FX_DCHECK(contenders_.count(contender_id));
  for (const auto response : responses) {
    if (auto resolution = RecordResponse(contender_id, response)) {
      return *resolution;
    }
  }

  return {};
}

std::optional<ContestResults> GestureArena::RecordResponse(ContenderId contender_id,
                                                           GestureResponse response) {
  const bool self_remove = response == GestureResponse::kNo;
  if (self_remove) {
    RemoveContender(contender_id);
  } else {
    AddResponseToQueue(contender_id, response);
  }

  std::optional<ContestResults> results = std::nullopt;
  if (auto resolution = TryResolve()) {
    results = *resolution;
    if (self_remove) {
      results->losers.push_back(contender_id);
    }
  } else if (self_remove) {
    results = ContestResults{.losers = {contender_id}};
  }

  return results;
}

void GestureArena::RemoveContender(ContenderId contender_id) {
  // Remove all traces of the contender represented by |contender_id|.
  const auto priority = contenders_.at(contender_id).priority;
  const auto num_removed1 = contenders_.erase(contender_id);
  FX_DCHECK(num_removed1 == 1);
  const auto num_removed2 = priority_to_id_.erase(priority);
  FX_DCHECK(num_removed2 == 1);
  for (auto& response_map : response_queue_) {
    response_map.erase(priority);
  }
}

void GestureArena::AddResponseToQueue(ContenderId contender_id, GestureResponse response) {
  Contender& contender = contenders_.at(contender_id);
  const size_t index = FindResponseIndex(response_queue_, contender.priority, QueueIsFullLength());
  // If the index is past the end of the queue, extend the queue.
  if (index == response_queue_.size()) {
    response_queue_.emplace_back();
  }
  response_queue_.at(index).insert_or_assign(contender.priority, response);
}

std::optional<ContestResults> GestureArena::TryResolve() {
  if (contenders_.empty()) {
    return ContestResults{.end_of_contest = true};
  }

  std::optional<ContestResults> results = std::nullopt;
  const bool can_resolve = AdvanceQueue();
  if (can_resolve) {
    std::vector<std::pair<ContenderId, GestureResponse>> ordered_responses;
    for (const auto& [priority, response] : response_queue_.front()) {
      ordered_responses.emplace_back(priority_to_id_.at(priority), response);
    }
    const ContenderId winner = Resolve(ordered_responses);
    results = SetUpWinner(winner);
  }

  return results;
}

bool GestureArena::AdvanceQueue() {
  const size_t num_contenders = contenders_.size();
  const bool all_responses_received = AllResponsesReceived();
  // Walk all complete sets of responses and try to resolve each response vector.
  bool can_resolve = false;
  while (!response_queue_.empty() && response_queue_.front().size() == num_contenders) {
    const bool at_sweep = all_responses_received && response_queue_.size() == 1;
    can_resolve = at_sweep ? CanResolveAtSweep(response_queue_.front())
                           : CanResolveMidContest(response_queue_.front());
    if (can_resolve || at_sweep) {
      break;
    } else {
      response_queue_.pop_front();
      --response_queue_expected_size_;
    }
  }

  return can_resolve;
}

ContestResults GestureArena::SetUpWinner(ContenderId id) {
  const auto contender = contenders_.at(id);
  contenders_.erase(id);

  ContestResults results{.winner = id, .end_of_contest = true};
  // Mark all remaining contenders as losers.
  for (const auto& [loser_id, _] : contenders_) {
    results.losers.emplace_back(loser_id);
  }

  contenders_.clear();
  priority_to_id_.clear();

  priority_to_id_.emplace(contender.priority, id);
  contenders_.insert({id, contender});

  contest_has_ended_ = true;
  return results;
}

std::vector<ContenderId> GestureArena::contenders() const {
  std::vector<ContenderId> contenders;
  for (const auto& [id, _] : contenders_) {
    contenders.emplace_back(id);
  }
  return contenders;
}

}  // namespace scenic_impl::input
