// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_GESTURE_ARENA_H_
#define SRC_UI_SCENIC_LIB_INPUT_GESTURE_ARENA_H_

#include <lib/fit/function.h>

#include <deque>
#include <map>
#include <unordered_map>
#include <vector>

#include "src/ui/scenic/lib/input/gesture_contender.h"

namespace scenic_impl::input {

struct ContestResults {
  std::optional<ContenderId> winner;
  std::vector<ContenderId> losers;
  bool end_of_contest = false;
};

// Class for deciding Gesture Disambiguation contests.
// On construction the arena takes a list of all the clients contending, in priority order, for the
// stream. The arena then receives responses for the every contender, and uses these in
// combination with priority to decide the owner ("winner") of the stream.
// If there's only a single contender then the contest is immediately decided in favor of that
// contender.
//
// Intended use:
// InputSystem creates one GestureArena every time a new stream begins, designating contenders
// for that stream at construction.
// Each time a new set of events arrives for the stream InputSystem should call UpdateStream() with
// the number of new events as well a bool telling the arena whether there will be any more events.
// RecordResponse() should be called once for each event for every contender (until the contender
// has been designated either a winner or a loser). With every call the arena makes an attempt at
// determining a winner, returning a ContestResults struct containing any new results from
// the contest.
// After the contest ends the arena can be kept around to track stream and winner state, but no more
// calls to RecordResponse() should be made.
class GestureArena {
 public:
  // Priority of each contender (only used internally). The lowest number equals highest priority.
  using Priority = int64_t;
  static constexpr Priority kInvalidPriority = std::numeric_limits<Priority>::min();

  struct Contender {
    ContenderId id = kInvalidContenderId;
    Priority priority = kInvalidPriority;
    GestureResponse response_status = GestureResponse::kUndefined;
  };

  // |contenders| should have no duplicates and be in priority order from highest to lowest.
  explicit GestureArena(std::vector<ContenderId> contenders);
  ~GestureArena() = default;

  // Update the stream with new messages. |length| denotes how many new messages were added to the
  // stream, while |is_last_message| denotes whether the stream ended with the last message in
  // the sequence.
  void UpdateStream(uint64_t added_length, bool is_last_message);

  // To be called whenever a contender has a new set of responses. The responses should be
  // chronologically ordered, with the earliest response first.
  // To remove a contender, pass in a NO response.
  ContestResults RecordResponse(ContenderId contender_id,
                                const std::vector<GestureResponse>& responses);

  // Returns a vector of all remaining contenders.
  std::vector<ContenderId> contenders() const;

  bool stream_has_ended() const { return stream_has_ended_; }
  bool contest_has_ended() const { return contest_has_ended_; }
  bool contains(ContenderId contender_id) const { return contenders_.count(contender_id) > 0; }

 private:
  void AddResponse(ContenderId contender_id, GestureResponse response);

  // See if the current state can determine a winner.
  std::optional<ContenderId> TryResolve();

  // Removes contender |contender_id| from the arena.
  // Should only be called once per contender.
  void RemoveContender(ContenderId contender_id);

  // Makes |id| the only remaining contender and return the resulting ContestResults.
  ContestResults SetWinner(ContenderId id);

  // All current contenders.
  std::unordered_map<ContenderId, Contender> contenders_;
  std::unordered_map<Priority, ContenderId> priority_to_id_;

  uint64_t stream_length_ = 0;
  bool stream_has_ended_ = false;
  bool contest_has_ended_ = false;

  // A double ended queue that collects responses from all contenders in chronological order.
  // Each item in the queue is a map of one response from every client, ordered by client priority
  // (highest priority to lowest).
  // When the map at the front of the queue has a response from every contender (oldest full map),
  // we inspect that map to determine how the contest should progress and then drop that map from
  // the deque.
  // - Mid contest. We append a new map to the queue for event's set of responses.
  // - Sweep. If a contender has Hold, an updated response is substituted directly into the final
  // response map (we don't append another response map).
  std::deque<std::map<Priority, GestureResponse>> responses_;

  // The stream index the frontmost item in |responses_| maps to.
  size_t index_of_current_responses_ = 0;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_GESTURE_ARENA_H_
