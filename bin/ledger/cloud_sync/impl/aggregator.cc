// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/cloud_sync/impl/aggregator.h"

#include <memory>

#include <lib/fxl/logging.h>

#include "peridot/bin/ledger/cloud_sync/public/sync_state_watcher.h"

namespace cloud_sync {
class Aggregator::Listener : public SyncStateWatcher {
 public:
  explicit Listener(Aggregator* aggregator);
  ~Listener() override;

  // Notify the client watcher, if present, of a new state.
  void Notify(SyncStateWatcher::SyncStateContainer sync_state) override;

  SyncStateWatcher::SyncStateContainer GetCurrentState();

 private:
  SyncStateWatcher::SyncStateContainer state_;

  Aggregator* aggregator_;
};

Aggregator::Listener::Listener(Aggregator* aggregator)
    : aggregator_(aggregator) {}

Aggregator::Listener::~Listener() { aggregator_->UnregisterListener(this); }

void Aggregator::Listener::Notify(
    SyncStateWatcher::SyncStateContainer sync_state) {
  state_ = sync_state;
  aggregator_->NewStateAvailable();
}

SyncStateWatcher::SyncStateContainer Aggregator::Listener::GetCurrentState() {
  return state_;
}

Aggregator::Aggregator() {}

Aggregator::~Aggregator() {
  // There should be no listener left when destroying this object.
  FXL_DCHECK(listeners_.empty());
}

void Aggregator::SetBaseWatcher(SyncStateWatcher* base_watcher) {
  base_watcher_ = base_watcher;
  if (base_watcher_) {
    base_watcher_->Notify(state_);
  }
}

std::unique_ptr<SyncStateWatcher> Aggregator::GetNewStateWatcher() {
  std::unique_ptr<Listener> listener = std::make_unique<Listener>(this);
  listeners_.insert(listener.get());
  return listener;
}

void Aggregator::UnregisterListener(Listener* listener) {
  listeners_.erase(listener);
}

void Aggregator::NewStateAvailable() {
  SyncStateWatcher::SyncStateContainer new_state;
  for (Aggregator::Listener* listener : listeners_) {
    new_state.Merge(listener->GetCurrentState());
  }
  if (new_state != state_) {
    state_ = new_state;
    if (base_watcher_) {
      base_watcher_->Notify(state_);
    }
  }
}

}  // namespace cloud_sync
