// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_UTILS_DISPATCHER_HOLDER_H_
#define SRC_UI_SCENIC_LIB_UTILS_DISPATCHER_HOLDER_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/event.h>

namespace utils {

// Abstract interface for objects which hold a dispatcher.  The purpose of this is to allow
// shared ownership of the dispatcher, which is often not otherwise possible.  For example,
// async::Loop has unique ownership of its dispatcher, so if you want to keep the dispatcher
// alive, you need to keep the loop alive.  But then, why not pass around a shared_ptr<AsyncLoop>?
// Because it's not always an async::Loop.  This interface hides the concrete type of the
// dispatcher's owner.
class DispatcherHolder {
 public:
  DispatcherHolder() = default;
  virtual ~DispatcherHolder() = default;

  // DispatcherHolder and subclasses cannot be copied nor moved.
  DispatcherHolder(const DispatcherHolder&) = delete;
  DispatcherHolder& operator=(const DispatcherHolder&) = delete;
  DispatcherHolder(DispatcherHolder&&) = delete;
  DispatcherHolder& operator=(DispatcherHolder&&) = delete;

  virtual async_dispatcher_t* dispatcher() const = 0;
};

// Concrete implementation of DispatcherHolder which wraps an async::Loop.
class LoopDispatcherHolder : public DispatcherHolder {
 public:
  explicit LoopDispatcherHolder(const async_loop_config_t* config) : loop_(config) {}

  async_dispatcher_t* dispatcher() const override { return loop_.dispatcher(); }

  async::Loop& loop() { return loop_; }

 private:
  async::Loop loop_;
};

// Concrete implementation of DispatcherHolder which doesn't own the dispatcher: the client is
// responsible for ensuring that the dispatcher outlives this object.  Typically used for testing.
class UnownedDispatcherHolder : public DispatcherHolder {
 public:
  explicit UnownedDispatcherHolder(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {}

  async_dispatcher_t* dispatcher() const override { return dispatcher_; }

 private:
  async_dispatcher_t* const dispatcher_;
};

}  // namespace utils

#endif  // SRC_UI_SCENIC_LIB_UTILS_DISPATCHER_HOLDER_H_
