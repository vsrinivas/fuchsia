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
  virtual void Push(T item, StatusCallback sent) = 0;
  // Default implementation of multi-push.
  // Derived types are encouraged but not required to replace this.
  virtual void PushMany(T* items, size_t count, StatusCallback sent);
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
void Sink<T>::PushMany(T* items, size_t count, StatusCallback done) {
  class Pusher {
   public:
    Pusher(Sink* sink, T* items, size_t count, StatusCallback done)
        : sink_(sink), done_(std::move(done)) {
      items_.reserve(count);
      for (size_t i = 0; i < count; i++) {
        items_.emplace_back(std::move(items[i]));
      }
      it_ = items_.begin();
      Next();
    }

   private:
    void Next() {
      auto push = it_;
      ++it_;
      if (it_ == items_.end()) {
        sink_->Push(std::move(*push), std::move(done_));
        delete this;
      } else {
        sink_->Push(std::move(*push), StatusCallback([this](Status&& status) {
                      if (status.is_ok()) {
                        Next();
                      } else {
                        done_(std::forward<Status>(status));
                        delete this;
                      }
                    }));
      }
    }

    Sink* const sink_;
    std::vector<T> items_;
    typename std::vector<T>::iterator it_;
    StatusCallback done_;
  };
  if (count == 0) {
    done(Status::Ok());
    return;
  }
  new Pusher(this, items, count, std::move(done));
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
