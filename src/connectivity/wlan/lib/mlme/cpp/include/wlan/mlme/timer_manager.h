// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_TIMER_MANAGER_H_
#define SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_TIMER_MANAGER_H_

#include <fbl/unique_ptr.h>
#include <wlan/mlme/timer.h>

#include <memory>
#include <queue>
#include <unordered_map>

namespace wlan {

class TimeoutId {
 public:
  TimeoutId() = default;

  bool operator==(const TimeoutId& that) const { return id_ == that.id_; }

  bool operator!=(const TimeoutId& that) const { return !(*this == that); }

 private:
  template <typename Event>
  friend class TimerManager;
  uint64_t id_ = 0;

  explicit TimeoutId(uint64_t id) : id_(id) {}
};

template <typename Event = std::tuple<>>
class TimerManager {
 public:
  explicit TimerManager(fbl::unique_ptr<Timer> timer)
      : timer_(std::move(timer)) {}

  // If the call succeeds, the returned TimeoutId is guaranteed to not be equal
  // to 'TimeoutId{}'
  zx_status_t Schedule(zx::time deadline, const Event& event,
                       TimeoutId* out_id) {
    if (heap_.empty() || deadline < heap_.top().deadline) {
      zx_status_t status = timer_->SetTimer(deadline);
      if (status != ZX_OK) {
        if (out_id != nullptr) {
          out_id->id_ = 0;
        }
        return status;
      }
    }
    uint64_t id = ++next_id_;
    heap_.push({deadline, id});
    events_.insert({id, event});
    if (out_id != nullptr) {
      out_id->id_ = id;
    }
    return ZX_OK;
  }

  template <typename EventCallback>
  zx_status_t HandleTimeout(EventCallback&& callback) {
    auto now = Now();
    while (!heap_.empty() && now >= heap_.top().deadline) {
      auto e = heap_.top();
      heap_.pop();
      auto node = events_.extract(e.id);
      if (node) {
        callback(now, node.mapped(), TimeoutId{e.id});
      }
    }
    // Remove canceled events from the heap before scheduling the next timer
    while (!heap_.empty() && events_.count(heap_.top().id) == 0) {
      heap_.pop();
    }
    if (heap_.empty()) {
      return ZX_OK;
    }
    return timer_->SetTimer(heap_.top().deadline);
  }

  zx::time Now() const { return timer_->Now(); }

  size_t NumScheduled() const { return events_.size(); }

  void Cancel(TimeoutId id) {
    // If the nearest timeout is being canceled, we choose not to reset the
    // timer in order to avoid error handling. Instead, HandleTimeout() will
    // filter out canceled events.
    events_.erase(id.id_);
  }

  void CancelAll() {
    // std::priority_queue has no ::clear() for some reason
    while (!heap_.empty()) {
      heap_.pop();
    }
    events_.clear();
    timer_->CancelTimer();
  }

 private:
  struct HeapItem {
    zx::time deadline;
    uint64_t id;
    bool operator<(const HeapItem& that) const {
      if (deadline > that.deadline) {
        return true;
      }
      if (deadline < that.deadline) {
        return false;
      }
      return id > that.id;
    }
  };

  fbl::unique_ptr<Timer> timer_;
  std::priority_queue<HeapItem> heap_;
  std::unordered_map<uint64_t, Event> events_;
  uint64_t next_id_ = 0;
};

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_LIB_MLME_CPP_INCLUDE_WLAN_MLME_TIMER_MANAGER_H_
