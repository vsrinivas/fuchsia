// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "lib/ftl/logging.h"

namespace media {

// CallbackJoiner is used to take action after multiple 'child' operations are
// completed.
//
// A CallbackJoiner maintains a counter of child operations and will call a
// callback when the counter is zero. The Spawn method signals the start of a
// child operation (increments the counter), and the Complete method signals
// the end of a child operation (decrements the counter). NewCallback combines
// these methods by first calling Spawn and then returning a callback that calls
// Complete.
//
// A single callback may be registered using the WhenJoined method. The callback
// is called when the child operation counter reaches zero, immediately if the
// counter is zero when WhenJoined is called.
//
// Here's an example of how to use CallbackJoiner to wait for multiple
// callbacks:
//
//    std::shared_ptr<CallbackJoiner> callback_joiner =
//        CallbackJoiner::Create();
//    doSomethingAsync(callback_joiner->NewCallback());
//    doSomethingElseAsync(callback_joiner->NewCallback());
//
//    callback_joiner->Spawn();
//    doSomethingAsyncWithAResult([callback_joiner](Result result) {
//      // ...
//      callback_joiner->Complete();
//    });
//
//    callback_joiner->WhenJoined(join_callback);
//
// Note that the CallbackJoiner is kept alive as long as the three callbacks
// still exist, because each callback captures a shared pointer to the
// CallbackJoiner.
//
class CallbackJoiner : public std::enable_shared_from_this<CallbackJoiner> {
 public:
  // Creates a CallbackJoiner and returns a shared pointer to it.
  // CallbackJoiners created in this way can safely create callbacks via the
  // NewCallback method.
  static std::shared_ptr<CallbackJoiner> Create() {
    return std::make_shared<CallbackJoiner>();
  }

  // Constructs a CallbackJoiner. NOTE: CallbackJoiner::NewCallback only works
  // for CallbackJoiners that already have shared pointers to them. The static
  // Create method is recommended for CallbackJoiners whose NewCallback method
  // will be invoked.
  CallbackJoiner();

  ~CallbackJoiner();

  // Indicates the initiation of a child operation. Every call to Spawn should
  // be matched by a subsequent call to Complete.
  void Spawn() { ++counter_; }

  // Indicates the completion of a child operation.
  void Complete() {
    FTL_DCHECK(counter_ != 0);
    --counter_;
    if (counter_ == 0) {
      std::function<void()> join_callback;
      join_callback = join_callback_;
      join_callback_ = nullptr;
      join_callback();
    }
  }

  // Calls Spawn and returns a new callback, which calls Complete. THIS METHOD
  // WILL ONLY WORK IF THERE IS ALREADY A SHARED POINTER TO THIS OBJECT.
  std::function<void()> NewCallback() {
    Spawn();
    std::shared_ptr<CallbackJoiner> this_ptr = shared_from_this();
    FTL_DCHECK(!this_ptr.unique());
    return [this_ptr]() {
      FTL_DCHECK(this_ptr);
      this_ptr->Complete();
    };
  }

  // Specifies a callback to be called when all child operations have completed.
  // If no child operations are currently pending, the callback is called
  // immediately. If child operations are pending, the callback is copied. The
  // copy called later (and reset) when all child oeprations have completed.
  // Only one callback at a time can be registered with WhenJoined.
  void WhenJoined(const std::function<void()>& join_callback) {
    FTL_DCHECK(!join_callback_);
    if (counter_ == 0) {
      join_callback();
    } else {
      join_callback_ = join_callback;
    }
  }

  // Cancels a callback registered with WhenJoined if it hasn't run yet. The
  // return value indicates whether a callback was cancelled.
  bool Cancel() {
    bool result = !!join_callback_;
    join_callback_ = nullptr;
    return result;
  }

 private:
  size_t counter_ = 0;
  std::function<void()> join_callback_;
};

}  // namespace media
