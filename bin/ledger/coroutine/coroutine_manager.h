// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_COROUTINE_COROUTINE_MANAGER_H_
#define PERIDOT_BIN_LEDGER_COROUTINE_COROUTINE_MANAGER_H_

#include <algorithm>
#include <list>

#include <lib/fit/function.h>

#include "lib/fxl/functional/auto_call.h"
#include "peridot/bin/ledger/coroutine/coroutine.h"

namespace coroutine {
// CoroutineManager manages the lifetime of coroutines.
class CoroutineManager {
 public:
  explicit CoroutineManager(CoroutineService* service) : service_(service) {}

  ~CoroutineManager() {
    // Interrupt any active handlers.
    while (!handlers_.empty()) {
      (*handlers_.begin())->Resume(coroutine::ContinuationStatus::INTERRUPTED);
    }
  }

  CoroutineManager(const CoroutineManager&) = delete;
  const CoroutineManager& operator=(const CoroutineManager&) = delete;

  // Starts a managed coroutine. This coroutine will be automatically
  // interrupted if this |CoroutineManager| object is destroyed.
  //
  // |callback| must be a callable object
  // |runnable| must be a callable object with the following signature:
  //   void(CoroutineHandler*, fit::function<void(Args...)>)
  // When the second argument of |runnable| is called, the coroutine is
  // unregistered from the manager object and |callback| is called with the same
  // arguments. It is an error to exit the coroutine without calling
  // |runnable|'s callback.
  template <typename Callback, typename Runnable>
  void StartCoroutine(Callback callback, Runnable runnable) {
    service_->StartCoroutine(
        [this, final_callback = std::move(callback),
         runnable = std::move(runnable)](CoroutineHandler* handler) mutable {
          auto callback =
              UpdateActiveHandlersCallback(handler, std::move(final_callback));

          runnable(handler, std::move(callback));

          // Verify that the handler is correctly unregistered. It would be a
          // bug otherwise.
          FXL_DCHECK(std::find(handlers_.begin(), handlers_.end(), handler) ==
                     handlers_.end());
        });
  }

 private:
  // Immediately adds the |handler| in the set of active ones, and once the
  // returned callback is called, removes the |handler| from the set, and calls
  // the given |callback|.
  template <typename Callback>
  auto UpdateActiveHandlersCallback(coroutine::CoroutineHandler* handler,
                                    Callback callback) {
    auto iter = handlers_.insert(handlers_.cend(), handler);
    return [this, iter, callback = std::move(callback)](auto... args) {
      // Remove the handler before calling the final callback. Otherwise the
      // handler might be unnecessarily interrupted, if this PageStorage
      // destructor is called in the callback.
      handlers_.erase(iter);
      callback(std::move(args)...);
    };
  }

  std::list<coroutine::CoroutineHandler*> handlers_;
  CoroutineService* const service_;
};

}  // namespace coroutine

#endif  // PERIDOT_BIN_LEDGER_COROUTINE_COROUTINE_MANAGER_H_
