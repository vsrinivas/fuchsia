// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_COROUTINE_COROUTINE_H_
#define PERIDOT_BIN_LEDGER_COROUTINE_COROUTINE_H_

#include <functional>

#include <lib/callback/capture.h>
#include <lib/fit/function.h>
#include <lib/fxl/functional/auto_call.h>
#include <lib/fxl/memory/ref_counted.h>
#include <lib/fxl/memory/ref_ptr.h>

// This Coroutine library allows to use coroutines. A coroutine is a function
// that can interrupt itself by yielding, and the computation will resume at the
// same point when another context of execution resumes the coroutine using
// its handler.
namespace coroutine {

// The status of a coroutine when it returns from Yield.
enum class ContinuationStatus : bool {
  // The coroutine is in its standard state. Computation can continue.
  OK,
  // The coroutine has been interrupted, it must unwind its stack and terminate.
  INTERRUPTED,
};

// The Handler of a coroutine. It allows a coroutine to yield and another
// context of execution to resume the computation.
//
// Threading: until the first Yield(), the coroutine executes on the thread that
// called CoroutineService::StartCoroutine(). Between Yield() and Resume(),
// the handler can be passed to another thread - the computation resumes on the
// thread that called Resume().
class CoroutineHandler {
 public:
  virtual ~CoroutineHandler() {}

  // Yield the current coroutine. This must only be called from inside the
  // coroutine associated with this handler. If Yield returns |INTERRUPTED|, the
  // coroutine must unwind its stack and terminate.
  FXL_WARN_UNUSED_RESULT virtual ContinuationStatus Yield() = 0;

  // Restarts the computation of the coroutine associated with this handler.
  // This must only be called outside of the coroutine when it is yielded. If
  // |status| is |INTERRUPTED|, |Yield| will return |INTERRUPTED| when the
  // coroutine is resumed, asking it to terminate.
  virtual void Resume(ContinuationStatus status) = 0;
};

// The service handling coroutines. It allows to create new coroutines.
// Destructing the service will terminate all active coroutines. All the
// non-terminated coroutines will eventually be activated and asked to
// terminate.
class CoroutineService {
 public:
  virtual ~CoroutineService() {}

  // Starts a new coroutine that will execute |runnable|.
  virtual void StartCoroutine(
      fit::function<void(CoroutineHandler*)> runnable) = 0;
};

// Allows to execute an asynchronous call in a coroutine. The coroutine will
// yield until the asynchronous call terminates, it will then be resumed and
// will store the results of the asynchronous calls in |parameters|. If
// |SyncCall| returns |INTERRUPTED|, the coroutine must unwind its stack and
// terminate.
//
// |async_call| will be never be called after this method returns. As such, it
// can capture local variables by reference.
//
// For instance, suppose you have the following asynchronous function
// LongAsyncComputation that signals its completion by passing the computed
// string and integer to a callback:
//
// void LongAsyncComputation(fit::function<void(std::string, int)> on_done);
//
// Here is how to execute it synchronously in a coroutine:
//
// CoroutineHandler* handler;
// std::string s; int i;
// if (SyncCall(handler, &LongAsyncComputation, &s, &i) ==
//     ContinuationStatus::INTERRUPTED) {
//   return ContinuationStatus::INTERRUPTED;
// }
// FXL_LOG(INFO) << "LongAsyncComputation returned: " << s << " " << i;
//
// Another usage pattern is to have a lambda in place of LongAsyncComputation,
// that will immediately store the callback provided by SyncCall in some
// ancillary data structure. The SyncCall will then yield until some other part
// of the code invokes this callback with a result:
//
// if (SyncCall(handler,
//              [this](fit::function<void(string, int)> on_done) {
//                pending_callbacks_.emplace_back(std::move(on_done));
//              },
//              &s, &i) == ContinuationStatus::INTERRUPTED) {
//   return ContinuationStatus::INTERRUPTED;
// }
// FXL_LOG(INFO) << "Some background task computed: " << s << " " << i;
//
template <typename A, typename... Args>
FXL_WARN_UNUSED_RESULT ContinuationStatus SyncCall(CoroutineHandler* handler,
                                                   A async_call,
                                                   Args*... parameters) {
  class TerminationSentinel
      : public fxl::RefCountedThreadSafe<TerminationSentinel> {
   public:
    bool terminated = false;
  };

  auto termination_sentinel = fxl::MakeRefCounted<TerminationSentinel>();
  auto on_return = fxl::MakeAutoCall(
      [termination_sentinel] { termination_sentinel->terminated = true; });

  volatile bool sync_state = true;
  volatile bool callback_called = false;
  // Unblock the coroutine (by having it return early) if the asynchronous call
  // drops its callback without ever calling it.
  auto unblocker =
      fxl::MakeAutoCall([termination_sentinel, &handler, &sync_state] {
        if (termination_sentinel->terminated) {
          return;
        }

        if (sync_state) {
          sync_state = false;
          return;
        }
        handler->Resume(ContinuationStatus::INTERRUPTED);
      });
  auto capture = callback::Capture(
      [&sync_state, &callback_called, handler,
       unblocker = std::move(unblocker)]() mutable {
        // |capture| is already gated by the termination sentinel below. No need
        // to re-check here.

        unblocker.cancel();
        callback_called = true;
        if (sync_state) {
          sync_state = false;
          return;
        }
        handler->Resume(ContinuationStatus::OK);
      },
      parameters...);
  async_call([termination_sentinel,
              capture = std::move(capture)](Args... args) mutable {
    if (termination_sentinel->terminated) {
      return;
    }
    capture(std::forward<Args>(args)...);
  });
  // If sync_state is still true, the callback was not called. Yield until it
  // is.
  if (sync_state) {
    sync_state = false;
    return handler->Yield();
  }
  return callback_called ? ContinuationStatus::OK
                         : ContinuationStatus::INTERRUPTED;
};

}  // namespace coroutine

#endif  // PERIDOT_BIN_LEDGER_COROUTINE_COROUTINE_H_
