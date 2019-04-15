// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_COROUTINE_COROUTINE_MANAGER_H_
#define SRC_LEDGER_LIB_COROUTINE_COROUTINE_MANAGER_H_

#include <lib/fit/function.h>

#include <algorithm>
#include <list>

#include "src/ledger/lib/coroutine/coroutine.h"

namespace coroutine {
// CoroutineManager manages the lifetime of coroutines.
class CoroutineManager {
 public:
  explicit CoroutineManager(CoroutineService* service) : service_(service) {}

  ~CoroutineManager() {
    // Interrupt any active handlers.
    in_deletion_ = true;
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
        [this, callback = std::move(callback),
         runnable = std::move(runnable)](CoroutineHandler* handler) mutable {
          bool callback_called = false;
          auto iter = handlers_.insert(handlers_.cend(), handler);
          auto final_callback = [this, &callback_called, iter,
                                 callback = std::move(callback)](auto... args) {
            // Remove the handler before calling the final callback. Otherwise
            // the handler might be unnecessarily interrupted, if this object
            // destructor is called in the callback.
            handlers_.erase(iter);
            callback_called = true;
            if (!in_deletion_) {
              callback(std::move(args)...);
            }
          };

          runnable(handler, std::move(final_callback));

          // Verify that the handler is correctly unregistered. It would be a
          // bug otherwise.
          FXL_DCHECK(callback_called);
        });
  }

  // Starts a managed coroutine. This coroutine will be automatically
  // interrupted if this |CoroutineManager| object is destroyed.
  //
  // |runnable| must be a callable object with the following signature:
  //   void(CoroutineHandler*)
  template <typename Runnable>
  void StartCoroutine(Runnable runnable) {
    service_->StartCoroutine([this, runnable = std::move(runnable)](
                                 CoroutineHandler* handler) mutable {
      auto iter = handlers_.insert(handlers_.cend(), handler);
      runnable(handler);
      handlers_.erase(iter);
    });
  }

 private:
  bool in_deletion_ = false;
  std::list<coroutine::CoroutineHandler*> handlers_;
  CoroutineService* const service_;
};

}  // namespace coroutine

#endif  // SRC_LEDGER_LIB_COROUTINE_COROUTINE_MANAGER_H_
