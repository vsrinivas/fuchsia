// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_TOKENS_TOKENS_H_
#define SRC_CAMERA_LIB_TOKENS_TOKENS_H_

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <zircon/assert.h>

namespace camera {

// The SharedToken class associates an owned object with a thread-safe copyable deferred callback.
// This can be used to tie an object to its associated release or cleanup methods. When all copies
// of an instance are destroyed, the specified callback is invoked.
template <typename T>
class SharedToken {
 public:
  // Creates a new SharedToken. Upon destruction of the last copy of an instance, the callback is
  // executed immediately.
  explicit SharedToken(T object, fit::closure callback)
      : object_{new T(std::forward<T>(object)), [callback = std::move(callback)](auto ptr) {
                  callback();
                  std::default_delete<T>()(ptr);
                }} {}

  // Creates a new SharedToken. Upon destruction of the last copy of an instance, the callback is
  // posted as a task on the specified dispatcher. If dispatcher is null, the default dispatcher for
  // the caller's thread is used.
  explicit SharedToken(T object, fit::closure callback, async_dispatcher_t* dispatcher)
      : object_{new T(std::forward<T>(object)),
                [callback = std::move(callback),
                 dispatcher =
                     dispatcher ? dispatcher : async_get_default_dispatcher()](auto ptr) mutable {
                  async::PostTask(dispatcher,
                                  [callback = std::move(callback), ptr = std::unique_ptr<T>(ptr)] {
                                    callback();
                                    ptr = nullptr;
                                  });
                }} {
    ZX_ASSERT_MSG(dispatcher || async_get_default_dispatcher(),
                  "default dispatcher for thread is not set");
  }

  // These methods return a reference/pointer to the shared owned object, which remains valid for
  // the lifetime of the linked token. This method is itself thread-safe, however the underlying
  // object that it returns may not be.
  T& operator*() const { return *object_; }
  T* operator->() const { return object_.get(); }

 private:
  std::shared_ptr<T> object_;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_TOKENS_TOKENS_H_
