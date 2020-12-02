// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_INTERNAL_MESSAGE_HANDLER_H_
#define LIB_FIDL_CPP_INTERNAL_MESSAGE_HANDLER_H_

#include <lib/fidl/cpp/internal/logging.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fit/function.h>
#include <lib/fit/traits.h>
#include <zircon/types.h>

namespace fidl {
namespace internal {

// An interface for receiving FIDL messages.
//
// Used by |MessageReader| to call back into its client whenever it reads a
// message from the channel.
class MessageHandler {
 public:
  virtual ~MessageHandler();

  // A new message has arrived.
  //
  // The memory backing the message will remain valid until this method returns,
  // at which point the memory might or might not be deallocated.
  virtual zx_status_t OnMessage(HLCPPIncomingMessage message) = 0;

  // The channel from which the messages were being read is gone.
  //
  // The channel's peer might have been closed or the |MessageReader| might have
  // unbound from the channel. In either case, implementations that keep
  // per-channel state should reset their state when this method is called.
  virtual void OnChannelGone();
};

// A light-weight callback type specialized to functions taking
// a single |fidl::Message| argument, and returning |zx_status_t|.
// Additionally, it decodes the message using the given |fidl_type_t*|
// before passing the decoded message to the wrapped |Callable&& target|.
//
// This callback type uses less binary size than |fit::callback|,
// due to a smaller ops table and lack of a move operation.
// Similar to |fit::callback|, it disposes any lambda captures immediately
// after being invoked, hence it is single-use only.
class SingleUseMessageHandler {
 public:
  // This is the maximum size we allocate for storing a lambda.
  //
  // The generated code always captures a `fit::function` passed by the user,
  // similar to the following:
  //
  //     // Given FIDL method `FidlMethod() -> (string ret)`
  //     void FidlMethod(fit::function<zx_status_t(std::string ret)> callback) {
  //         proxy_controller.Send(request, std::make_unique<SingleUseMessageHandler>(
  //             [callback = std::move(callback)](fidl::Message&& message) {
  //                 std::string ret = fidl::DecodeAs<std::string>(message);
  //                 callback(ret);
  //             },
  //             &coding_table_for_the_response_type
  //         ));
  //     }
  //
  // Hence we only allocate as much as the size of the capture, here a `fit::function`.
  constexpr static size_t kCallableSize =
      sizeof(fit::function<zx_status_t(fidl::HLCPPIncomingMessage&& message)>);

  template <
      typename Callable,
      typename = std::enable_if_t<
          std::is_same<typename fit::callable_traits<Callable>::return_type, zx_status_t>::value>,
      typename =
          std::enable_if_t<std::is_same<typename fit::callable_traits<Callable>::args,
                                        fit::parameter_pack<fidl::HLCPPIncomingMessage&&>>::value>>
  constexpr explicit SingleUseMessageHandler(Callable&& target, const fidl_type_t* type) {
    static_assert(sizeof(Callable) <= kCallableSize,
                  "Callable is too big to store in a SingleUseMessageHandler");
    new (&storage_) Callable(std::forward<Callable>(target));
    invoke_ = InvokeImpl<Callable>;
    destroy_ = DestroyImpl<Callable>;
    type_ = type;
  }

  SingleUseMessageHandler(const SingleUseMessageHandler&) = delete;
  SingleUseMessageHandler& operator=(const SingleUseMessageHandler&) = delete;

  SingleUseMessageHandler(SingleUseMessageHandler&&) = delete;
  SingleUseMessageHandler& operator=(SingleUseMessageHandler&&) = delete;

  zx_status_t operator()(fidl::HLCPPIncomingMessage message) {
    const char* error_msg = nullptr;
    zx_status_t status = message.Decode(type_, &error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_DECODING_ERROR(message, type_, error_msg);
      return status;
    }
    status = invoke_(this, std::move(message));
    invoke_ = nullptr;
    destroy_(this);
    return status;
  }

  ~SingleUseMessageHandler() {
    if (invoke_)
      destroy_(this);
  }

 private:
  template <typename Callable>
  static zx_status_t InvokeImpl(SingleUseMessageHandler* handler,
                                fidl::HLCPPIncomingMessage&& message) {
    auto& target = *reinterpret_cast<Callable*>(&handler->storage_);
    return target(std::move(message));
  }

  template <typename Callable>
  static void DestroyImpl(SingleUseMessageHandler* handler) {
    auto& target = *reinterpret_cast<Callable*>(&handler->storage_);
    target.~Callable();
  }

  typename std::aligned_storage<kCallableSize>::type storage_;
  zx_status_t (*invoke_)(SingleUseMessageHandler* handler, fidl::HLCPPIncomingMessage&&) = nullptr;
  void (*destroy_)(SingleUseMessageHandler* handler) = nullptr;
  const fidl_type_t* type_;
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_INTERNAL_MESSAGE_HANDLER_H_
