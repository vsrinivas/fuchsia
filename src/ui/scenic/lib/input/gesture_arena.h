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
// RecordResponses() should be called once for each event for every contender (until the contender
// has been designated either a winner or a loser). With every call the arena makes an attempt at
// determining a winner, returning a ContestResults struct containing any new results from
// the contest.
// After the contest ends the arena can be kept around to track stream and winner state, but no more
// calls to RecordResponses() should be made.
class GestureArena {
 public:
  // Priority of each contender (only used internally). The lowest number equals highest priority.
  using Priority = int64_t;
  static constexpr Priority kInvalidPriority = std::numeric_limits<Priority>::min();

  struct Contender {
    ContenderId id = kInvalidContenderId;
    Priority priority = kInvalidPriority;
  };

  // |contenders| should have no duplicates and be in priority order from highest to lowest.
  explicit GestureArena(std::vector<ContenderId> contenders);
  ~GestureArena() = default;

  // Update the stream with new messages. |new_message_count| denotes how many new messages were
  // added to the stream (and therefore how many messages we expect responses for), while
  // |is_last_message| denotes whether they were the last messages of the stream.
  void UpdateStream(uint64_t new_message_count, bool is_last_message);

  // To be called whenever a contender has a new set of responses. The responses should be
  // chronologically ordered, with the earliest response first.
  // To remove a contender, pass in a NO response.
  ContestResults RecordResponses(ContenderId contender_id,
                                 const std::vector<GestureResponse>& responses);

  // Returns a vector of all remaining contenders.
  std::vector<ContenderId> contenders() const;

  bool stream_has_ended() const { return stream_has_ended_; }
  bool contest_has_ended() const { return contest_has_ended_; }
  bool contains(ContenderId contender_id) const { return contenders_.count(contender_id) > 0; }

 private:
  // Records a response and if the response resolves the contest returns the ContestResults.
  // Otherwise returns std::nullopt.
  std::optional<ContestResults> RecordResponse(ContenderId contender_id, GestureResponse response);

  // Removes contender |contender_id| from the arena.
  // Should only be called once per contender.
  void RemoveContender(ContenderId contender_id);

  // Adds the |response| at the next spot in the queue for |contender_id|.
  void AddResponseToQueue(ContenderId contender_id, GestureResponse response);

  // Resolves the contest and returns the result if possible, otherwise advances the queue as far as
  // possible and returns std::nullopt.
  std::optional<ContestResults> TryResolve();

  // Pops as many items off the queue as currently possible or until it detects that the
  // contest is resolvable.
  // Returns whether the contest is resolvable.
  bool AdvanceQueue();

  // Makes |id| the only remaining contender and returns the resulting ContestResults.
  ContestResults SetUpWinner(ContenderId id);

  // Returns whether the queue has reached its full expected length; i.e. whether the last set of
  // responses for an interaction has been initialized.
  bool QueueIsFullLength() const {
    return stream_has_ended_ && response_queue_.size() == response_queue_expected_size_;
  }

  // Returns whether all the responses the queue will ever receive have been received.
  bool AllResponsesReceived() const {
    return QueueIsFullLength() && response_queue_.front().size() == contenders_.size();
  }

  // All current contenders.
  std::unordered_map<ContenderId, Contender> contenders_;
  std::unordered_map<Priority, ContenderId> priority_to_id_;

  bool stream_has_ended_ = false;
  bool contest_has_ended_ = false;

  // The expected size of the queue when all responses have been received.
  size_t response_queue_expected_size_ = 0;

  // A double ended queue that collects responses from all contenders in chronological order.
  // Each item in the queue is a map of one response from every client, ordered by client priority
  // (highest priority to lowest).
  // When the map at the front of the queue has a response from every contender (oldest full map),
  // we inspect that map to determine how the contest should progress and then drop that map from
  // the deque.
  // - Mid contest. We append a new map to the queue for event's set of responses.
  // - Sweep. If a contender has Hold, an updated response is substituted directly into the final
  // response map (we don't append another response map).
  std::deque<std::map<Priority, GestureResponse>> response_queue_;
};

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_GESTURE_ARENA_H_
