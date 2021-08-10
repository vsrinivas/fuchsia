// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/internal/natural_client_messenger.h>
#include <lib/fidl/cpp/message.h>
#include <lib/fidl/llcpp/client_base.h>
#include <lib/fidl/llcpp/result.h>

namespace fidl {
namespace internal {

// TODO(fxbug.dev/82189): Switch to new natural domain objects instead of HLCPP.
void NaturalClientMessenger::TwoWay(const fidl_type_t* type, HLCPPOutgoingMessage&& message,
                                    fidl::internal::ResponseContext* context) {
  client_base_->PrepareAsyncTxn(context);
  message.set_txid(context->Txid());

  fidl::Result result = Send(type, std::move(message));
  if (!result.ok()) {
    client_base_->ForgetAsyncTxn(context);
    if (result.reason() == fidl::Reason::kUnbind) {
      context->OnCanceled();
    } else {
      // TODO(fxbug.dev/75324): Switch to new hook for propagating send-time errors.
      context->OnError(result);
    }
  }
}

// TODO(fxbug.dev/82189): Switch to new natural domain objects instead of HLCPP.
fidl::Result NaturalClientMessenger::OneWay(const fidl_type_t* type,
                                            HLCPPOutgoingMessage&& message) {
  message.set_txid(0);
  return Send(type, std::move(message));
}

// TODO(fxbug.dev/82189): Switch to new natural domain objects instead of HLCPP.
fidl::Result NaturalClientMessenger::Send(const fidl_type_t* type, HLCPPOutgoingMessage&& message) {
  const char* error_msg = nullptr;
  zx_status_t status = message.Validate(type, &error_msg);
  if (status != ZX_OK) {
    return fidl::Result::EncodeError(status, error_msg);
  }

  std::shared_ptr<fidl::internal::ChannelRef> channel = client_base_->GetChannel();
  if (!channel) {
    return fidl::Result::Unbound();
  }

  status = message.Write(channel->handle(), 0);
  if (status != ZX_OK) {
    return fidl::Result::TransportError(status);
  }

  return fidl::Result::Ok();
}

}  // namespace internal
}  // namespace fidl
