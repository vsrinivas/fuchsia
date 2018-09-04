// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TESTING_LOOP_CONTROLLER_H_
#define PERIDOT_BIN_LEDGER_TESTING_LOOP_CONTROLLER_H_

#include <functional>
#include <memory>

#include <lib/fit/function.h>
#include <lib/fxl/memory/ref_ptr.h>

#include "peridot/bin/ledger/fidl/include/types.h"

namespace ledger {
// Helper class for waiting for asynchronous event.
// For a given |CallbackWaiter|, one can retrieve a callback through
// |GetCallback|. The callback must be called when the asynchronous event
// ends.
// When |RunUntilCalled| is called, it will run the event loop, until the
// callback from |GetCallback| is called.
// If one is waiting for the callback to be called multiple times, one can
// execute |RunUntilCalled| multiple times. The |n|th run of |RunUntilCalled|
// will return once the callback have been called at least |n| time.
// |GetCallback| can be called multiple time, and all the returned callback
// will be equivalent.
class CallbackWaiter {
 public:
  CallbackWaiter() {}
  virtual ~CallbackWaiter() {}
  virtual fit::function<void()> GetCallback() = 0;
  // TODO: return whether the callback waiter has been called.
  virtual void RunUntilCalled() = 0;
  // Returns whether the next expected calback has not already been called. If
  // |false|, |RunUntilCalled| will return immediately.
  virtual bool NotCalledYet() = 0;
};

// A subloop.
class SubLoop {
 public:
  virtual ~SubLoop() {}
  // Returns a dispatcher whose runloop is owned by |this|.
  virtual async_dispatcher_t* dispatcher() = 0;
};

// Controller for the main run loop. This allows to control the loop that will
// call the factory and the multiple instances.
class LoopController {
 public:
  virtual ~LoopController(){};

  // Runs the loop.
  virtual void RunLoop() = 0;
  // Stops the loop.
  virtual void StopLoop() = 0;
  // Starts a new subloop.
  virtual std::unique_ptr<SubLoop> StartNewLoop() = 0;
  // Returns a waiter that can be used to run the loop until a callback has
  // been called.
  std::unique_ptr<CallbackWaiter> NewWaiter();
  // Returns the dispatcher.
  virtual async_dispatcher_t* dispatcher() = 0;
  // Returns a closure that triggers the closing of the loop.
  virtual fit::closure QuitLoopClosure() = 0;
  // Runs the loop until |condition| returns true.
  virtual bool RunLoopUntil(fit::function<bool()> condition) = 0;
  // Runs the loop until |duration| as passed.
  // Returns false if the loop was ran for less than |duration|.
  // Returns true otherwise.
  virtual bool RunLoopFor(zx::duration duration) = 0;
};

}  // namespace ledger

#endif  // PERIDOT_BIN_LEDGER_TESTING_LOOP_CONTROLLER_H_
