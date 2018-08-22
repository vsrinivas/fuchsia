// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>
#include "callback.h"
#include "optional.h"
#include "status.h"

namespace overnet {

// A typed data sink for some kind of object
template <class T>
class Sink {
 public:
  virtual ~Sink() {}
  virtual void Close(const Status& status, Callback<void> quiesced) = 0;
  virtual void Push(T item) = 0;
  void PushMany(T* items, size_t count);
};

template <class T>
class Source {
 public:
  virtual ~Source() {}
  virtual void Close(const Status& status) = 0;
  virtual void Pull(StatusOrCallback<Optional<T>> ready) = 0;
  // Default implementation of a draining-pull.
  // Derived types are encouraged but not required to replace this.
  // Implies a Close() call.
  virtual void PullAll(StatusOrCallback<std::vector<T>> ready);
};

template <class T>
void Sink<T>::PushMany(T* items, size_t count) {
  for (size_t i = 0; i < count; i++) {
    Push(items[i]);
  }
}

template <class T>
void Source<T>::PullAll(StatusOrCallback<std::vector<T>> ready) {
  class Puller {
   public:
    Puller(Source* source, StatusOrCallback<std::vector<T>> ready)
        : source_(source), ready_(std::move(ready)) {
      Next();
    }

   private:
    void Next() {
      source_->Pull(
          StatusOrCallback<Optional<T>>([this](StatusOr<Optional<T>>&& status) {
            if (status.is_error() || !status->has_value()) {
              source_->Close(status.AsStatus());
              ready_(std::move(so_far_));
              delete this;
              return;
            }
            so_far_.emplace_back(std::move(**status.get()));
            Next();
          }));
    }

    Source* const source_;
    std::vector<T> so_far_;
    StatusOrCallback<std::vector<T>> ready_;
  };
  new Puller(this, std::move(ready));
}

}  // namespace overnet
