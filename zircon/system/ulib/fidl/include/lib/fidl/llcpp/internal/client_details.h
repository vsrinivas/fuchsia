// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_CLIENT_DETAILS_H_
#define LIB_FIDL_LLCPP_INTERNAL_CLIENT_DETAILS_H_

#include <lib/fidl/llcpp/result.h>
#include <lib/fit/function.h>
#include <lib/stdcompat/variant.h>

#include <memory>
#include <type_traits>

namespace fidl {
namespace internal {

// The base class for all asynchronous event handlers, regardless of domain
// object flavor or protocol type.
class AsyncEventHandler {
 public:
  virtual ~AsyncEventHandler() = default;

  // |on_fidl_error| is invoked when the client encounters a terminal error:
  //
  // - The server-end of the channel was closed.
  // - An epitaph was received.
  // - Decoding or encoding failed.
  // - An invalid or unknown message was encountered.
  // - Error waiting on, reading from, or writing to the channel.
  //
  // It uses snake-case to differentiate from virtual methods corresponding to
  // FIDL events.
  //
  // |info| contains the detailed reason for stopping message dispatch.
  //
  // |on_fidl_error| will be invoked on a dispatcher thread, unless the user
  // shuts down the async dispatcher while there are active client bindings
  // associated with it. In that case, |on_fidl_error| will be synchronously
  // invoked on the thread calling dispatcher shutdown.
  virtual void on_fidl_error(::fidl::UnbindInfo error) {}
};

}  // namespace internal

// A type-erasing object to inform the user the completion of bindings teardown.
//
// Teardown observers are constructed by public helper functions such as
// |fidl::ObserveTeardown|. Adding this layer of indirection allows extending
// teardown observation to custom user types (for example, by defining another
// helper function) without changing this class.
class AnyTeardownObserver final {
 public:
  // Creates an observer that notifies teardown completion by destroying
  // |object|.
  template <typename T>
  static AnyTeardownObserver ByOwning(T object) {
    return AnyTeardownObserver([object = std::move(object)] {});
  }

  // Creates an observer that notifies teardown completion by invoking
  // |callback|, then destroying |callback|.
  template <typename Callable>
  static AnyTeardownObserver ByCallback(Callable&& callback) {
    return AnyTeardownObserver(fit::closure(std::forward<Callable>(callback)));
  }

  // Creates an observer that does nothing on teardown completion.
  static AnyTeardownObserver Noop() {
    return AnyTeardownObserver([] {});
  }

  // Notify teardown completion. This consumes the observer.
  void Notify() && { callback_(); }

  AnyTeardownObserver(const AnyTeardownObserver& other) noexcept = delete;
  AnyTeardownObserver& operator=(const AnyTeardownObserver& other) noexcept = delete;
  AnyTeardownObserver(AnyTeardownObserver&& other) noexcept = default;
  AnyTeardownObserver& operator=(AnyTeardownObserver&& other) noexcept = default;

  ~AnyTeardownObserver() {
    // |callback_| must be expended by the bindings runtime.
    ZX_DEBUG_ASSERT(callback_ == nullptr);
  }

 private:
  using Closure = fit::callback<void()>;

  explicit AnyTeardownObserver(Closure&& callback) : callback_(std::move(callback)) {
    ZX_DEBUG_ASSERT(callback_ != nullptr);
  }

  Closure callback_;
};

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_CLIENT_DETAILS_H_
