// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>
#include "sink.h"

namespace overnet {

// Adaptor class: allows broadcasting to a suite of sinks
template <class T>
class BroadcastSink final : public Sink<T> {
 public:
  BroadcastSink(StatusOrCallback<Sink<T>*> ready_to_start)
      : ready_to_start_(std::move(ready_to_start)) {}

  BroadcastSink(const BroadcastSink&) = delete;
  BroadcastSink& operator=(const BroadcastSink&) = delete;

  StatusOrCallback<Sink<T>*> AddTarget() {
    pending_targets_++;
    return StatusOrCallback<Sink<T>*>([this](const StatusOr<Sink<T>*>& status) {
      this->PendingTargetReady(status);
    });
  }

  void Close(const Status& status) override {
    for (auto* tgt : targets_) tgt->Close(status);
    closed_ = true;
    if (closed_ && pending_targets_ == 0) delete this;
  }

  void Push(T item, StatusCallback done) override {
    assert(pending_targets_ == 0 && error_.is_ok());
    push_done_ = std::move(done);
    // guard against instant completion within tgt->Push (which could cause us
    // to finish before we finish setting up)
    auto local_complete = AddPush();
    for (auto* tgt : targets_) tgt->Push(item, AddPush());
    local_complete(Status::Ok());
  }

 private:
  ~BroadcastSink() = default;

  StatusCallback AddPush() {
    pending_targets_++;
    return StatusCallback(
        [this](const Status& status) { this->PendingPushDone(status); });
  }

  void PendingTargetReady(const StatusOr<Sink<T>*>& status) {
    if (auto* sink = status.get()) {
      if (error_.is_ok()) {
        targets_.push_back(*sink);
      } else {
        (*sink)->Close(error_);
      }
    } else if (error_.is_ok()) {
      SawError(status.AsStatus());
    }
    if (0 == --pending_targets_) {
      ready_to_start_(error_.is_ok() ? StatusOr<Sink<T>*>(this)
                                     : StatusOr<Sink<T>*>(error_));
    }
  }

  void PendingPushDone(const Status& status) {
    if (error_.is_ok() && !status.is_ok()) {
      SawError(status);
    }
    if (0 == --pending_targets_) {
      push_done_(error_);
      if (closed_) delete this;
    }
  }

  void SawError(const Status& status) {
    assert(status.is_error());
    if (error_.is_error()) return;  // already saw an error
    error_ = status;
    for (auto* tgt : targets_) {
      tgt->Close(error_);
    }
    targets_.clear();
  }

  StatusOrCallback<Sink<T>*> ready_to_start_;
  StatusCallback push_done_;
  int pending_targets_ = 0;
  bool closed_ = false;
  std::vector<Sink<T>*> targets_;
  Status error_ = Status::Ok();
};

}  // namespace overnet