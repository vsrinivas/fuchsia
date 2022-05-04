// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_HELPERS_COLLECTION_TRACKER_H_
#define SRC_MEDIA_VNEXT_LIB_HELPERS_COLLECTION_TRACKER_H_

#include <lib/syslog/cpp/macros.h>

#include <unordered_map>

namespace fmlib {

// The classes in this file are used to help efficiently maintain a remote copy of a collection.
// If a remote collection is maintained using 'add', 'update', and 'remove' verbs,
// |CollectionTracker| can be used to aggregate add/update/remove actions made on the collection.
// When it comes time to send an update, the |CollectionTracker| produces a list of the actions
// that must be taken to update the remote collection. The list will contain at most one action
// per entry in the collection.
//
//     CollectionTracker<uint32_t> tracker;
//     // Call tracker.OnAdded/OnRemoved/OnUpdated many times.
//     if (tracker.is_dirty()) {
//       SendActionsToRemoteParty(tracker.Clean());
//     }
//
// |CollectionTracker::Clean| returns an unordered map of key/action pairs where 'key' is the key
// of the entry and 'action' is |kAdd|, |kUpdate|, or |kRemove|. It's up to the caller to prepare
// and send a message that specifies these actions.

// Actions to take to clean an entry in a collection.
enum class CleanAction { kNone, kAdd, kUpdate, kRemove };

// Tracks changes for a single entry in a collection.
class CollectionEntryTracker {
 public:
  // Indicates that an item has been added for this entry.
  void OnAdded();

  // Indicates that the item has been updated for this entry.
  void OnUpdated();

  // Indicates that an item has been removed for this entry.
  void OnRemoved();

  // Determines what action to take, if any, to clean this entry and updates the state assuming the
  // action is taken.
  CleanAction Clean();

  // Returns true if and only if the tracked entry is currently absent and was also absent when the
  // tracker was last clean. This method is typically used to determine whether this tracker can be
  // discarded.
  bool is_discardable() const { return state_ == State::kAbsent; }

  // Returns false if the |Clean| method will return |CleanAction::kNone|, returns true otherwise.
  bool is_dirty() const;

 private:
  enum class State : uint8_t { kAbsent, kRemoved, kPresent, kUpdated, kAdded };
  State state_ = State::kAbsent;
};

// Tracks changes for all entries in a collection. |T| is the key type for the collection.
template <typename T>
class CollectionTracker {
 public:
  CollectionTracker() = default;

  ~CollectionTracker() = default;

  // Indicates that an item has been added for key |key|.
  void OnAdded(const T& key) {
    auto& entry_tracker = entry_trackers_by_id_.try_emplace(key).first->second;
    bool was_dirty = entry_tracker.is_dirty();
    entry_tracker.OnAdded();
    TidyUp(key, entry_tracker, was_dirty);
  }

  // Indicates that an item has been updated for key |key|.
  void OnUpdated(const T& key) {
    auto& entry_tracker = entry_trackers_by_id_.try_emplace(key).first->second;
    bool was_dirty = entry_tracker.is_dirty();
    entry_tracker.OnUpdated();
    TidyUp(key, entry_tracker, was_dirty);
  }

  // Indicates that an item has been removed for key |key|.
  void OnRemoved(const T& key) {
    auto& entry_tracker = entry_trackers_by_id_.try_emplace(key).first->second;
    bool was_dirty = entry_tracker.is_dirty();
    entry_tracker.OnRemoved();
    TidyUp(key, entry_tracker, was_dirty);
  }

  // Determines what actions to take, if any, to clean this collection and updates the state
  // assuming the actions will be taken. The returned map will not contain any |kNone| actions.
  // Returns an empty map, if all entries are clean.
  std::unordered_map<T, CleanAction> Clean() {
    std::unordered_map<T, CleanAction> result;
    if (dirty_entries_ == 0) {
      return result;
    }

    for (auto iter = entry_trackers_by_id_.begin(); iter != entry_trackers_by_id_.end();) {
      auto& slot_id = iter->first;
      auto& entry_tracker = iter->second;

      auto action = entry_tracker.Clean();
      if (action != CleanAction::kNone) {
        result.emplace(slot_id, action);
      }

      if (entry_tracker.is_discardable()) {
        iter = entry_trackers_by_id_.erase(iter);
      } else {
        ++iter;
      }
    }

    dirty_entries_ = 0;

    return result;
  }

  // Returns true if the |Clean| method will return one or more actions, false if it will return
  // an empty map.
  bool is_dirty() const { return dirty_entries_ != 0; }

 private:
  // Updates |dirty_entries_| and removes the entry tracker if it's in initial state.
  void TidyUp(const T& key, const CollectionEntryTracker& entry_tracker, bool was_dirty) {
    if (entry_tracker.is_dirty()) {
      if (!was_dirty) {
        ++dirty_entries_;
      }

      FX_CHECK(!entry_tracker.is_discardable());
    } else {
      if (was_dirty) {
        FX_CHECK(dirty_entries_ != 0);
        --dirty_entries_;
      }

      if (entry_tracker.is_discardable()) {
        entry_trackers_by_id_.erase(key);
      }
    }
  }

  // |entry_trackers_by_id_| is kept free of entry trackers that are in initial state.
  std::unordered_map<T, CollectionEntryTracker> entry_trackers_by_id_;
  size_t dirty_entries_ = 0;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_HELPERS_COLLECTION_TRACKER_H_
