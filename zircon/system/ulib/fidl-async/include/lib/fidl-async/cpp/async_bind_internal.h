// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_ASYNC_CPP_ASYNC_BIND_INTERNAL_H_
#define LIB_FIDL_ASYNC_CPP_ASYNC_BIND_INTERNAL_H_

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fidl-async/cpp/async_bind.h>
#include <lib/fidl/epitaph.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/fit/function.h>
#include <lib/sync/completion.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

namespace fidl {

namespace internal {

// Thread safety token.
//
// This token acts like a "no-op mutex", allowing compiler thread safety annotations
// to be placed on code or data that should only be accessed by a particular thread.
// Any code that acquires the token makes the claim that it is running on the (single)
// correct thread, and hence it is safe to access the annotated data and execute the annotated code.
struct __TA_CAPABILITY("role") Token {};
class __TA_SCOPED_CAPABILITY ScopedToken {
 public:
  explicit ScopedToken(const Token& token) __TA_ACQUIRE(token) {}
  ~ScopedToken() __TA_RELEASE() {}
};

class AsyncTransaction;
class AsyncBinding;
struct Deleter;

using TypeErasedDispatchFn = bool (*)(void*, fidl_msg_t*, ::fidl::Transaction*);

using TypeErasedOnChannelCloseFn = fit::callback<void(void*)>;

// This class abstracts the binding of a channel, a single threaded dispatcher and an implementation
// of the llcpp bindings.
class AsyncBinding {
 public:
  // Creates a binding that stays bounded until either it is explicitly unbound via |Unbind|,
  // a peer close is recevied in the channel from the remote end, or all transactions generated
  // from it are destructed and an error occurred (either Close() is called from a transaction or
  // an internal error like not being able to write to the channel occur).
  // The binding is destroyed once no more references are held, including the one returned by
  // this static method.
  static std::shared_ptr<AsyncBinding> CreateSelfManagedBinding(
      async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
      TypeErasedDispatchFn dispatch_fn, TypeErasedOnChannelCloseFn on_channel_closing_fn,
      TypeErasedOnChannelCloseFn on_channel_closed_fn);
  ~AsyncBinding() __TA_REQUIRES(domain_token());

  void MessageHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                      const zx_packet_signal_t* signal) __TA_REQUIRES(domain_token());
  zx_status_t BeginWait() __TA_EXCLUDES(domain_token()) { return callback_.Begin(dispatcher_); }

  void Unbind() __TA_EXCLUDES(domain_token());

 protected:
  explicit AsyncBinding(async_dispatcher_t* dispatcher, zx::channel channel, void* impl,
                        TypeErasedDispatchFn dispatch_fn,
                        TypeErasedOnChannelCloseFn on_channel_closing_fn,
                        TypeErasedOnChannelCloseFn on_channel_closed_fn);

 private:
  friend fidl::internal::AsyncTransaction;
  friend fidl::BindingRef;

  struct Deleter {
    void operator()(internal::AsyncBinding* binding) const {
      if (binding->deleter_) {
        sync_completion_signal(binding->deleter_);
      }
      delete binding;
    }
  };

  zx::unowned_channel channel() const { return zx::unowned_channel(channel_); }
  const Token& domain_token() const __TA_RETURN_CAPABILITY(domain_token_) { return domain_token_; }
  void OnChannelClosing(zx_status_t epitaph) __TA_REQUIRES(domain_token());
  void Close(zx_status_t epitaph, std::shared_ptr<AsyncBinding> binding)
      __TA_EXCLUDES(domain_token());

  Token domain_token_ = {};
  async_dispatcher_t* dispatcher_ = nullptr;
  sync_completion_t* deleter_ = nullptr;
  zx::channel channel_ = {};
  void* interface_ = nullptr;
  TypeErasedDispatchFn dispatch_fn_ = {};
  TypeErasedOnChannelCloseFn on_channel_closing_fn_ __TA_GUARDED(domain_token()) = {};
  TypeErasedOnChannelCloseFn on_channel_closed_fn_ __TA_GUARDED(domain_token()) = {};
  zx_status_t epitaph_ __TA_GUARDED(domain_token()) = ZX_OK;
  async::WaitMethod<AsyncBinding, &AsyncBinding::MessageHandler> callback_{this};
  std::atomic<bool> closing_ = false;
  std::shared_ptr<AsyncBinding> keep_alive_ = {};
};

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_ASYNC_CPP_ASYNC_BIND_INTERNAL_H_
