// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/test/frobinator.h"

#include <zircon/assert.h>

#include <memory>

#include "lib/fidl/cpp/test/fidl_types.h"

namespace fidl {
namespace test {
namespace {

constexpr uint32_t kFrobinator_Frob_Ordinal = 1u;
constexpr uint32_t kFrobinator_Grob_Ordinal = 2u;

}  // namespace

Frobinator::~Frobinator() = default;

Frobinator_Sync::~Frobinator_Sync() = default;

Frobinator_Proxy::Frobinator_Proxy(
    ::fidl::internal::ProxyController* controller)
    : controller_(controller) {}

Frobinator_Proxy::~Frobinator_Proxy() = default;

void Frobinator_Proxy::Frob(::fidl::StringPtr value) {
  ::fidl::Encoder encoder(kFrobinator_Frob_Ordinal);
  size_t offset =
      encoder.Alloc(::fidl::CodingTraits<::fidl::StringPtr>::encoded_size);
  ::fidl::Encode(&encoder, &value, offset);
  controller_->Send(&unbounded_nonnullable_string_message_type,
                    encoder.GetMessage(), nullptr);
}

namespace {

class Frobinator_Grob_ResponseHandler
    : public ::fidl::internal::MessageHandler {
 public:
  Frobinator_Grob_ResponseHandler(Frobinator_Proxy::GrobCallback callback)
      : callback_(std::move(callback)) {
    ZX_DEBUG_ASSERT_MSG(callback_,
                        "Callback must not be empty for Frobinator.Grob\n");
  }

  zx_status_t OnMessage(::fidl::Message message) override {
    const char* error_msg = nullptr;
    zx_status_t status =
        message.Decode(&unbounded_nonnullable_string_message_type, &error_msg);
    if (status != ZX_OK)
      return status;
    ::fidl::Decoder decoder(std::move(message));
    size_t offset = sizeof(fidl_message_header_t);
    callback_(::fidl::DecodeAs<::fidl::StringPtr>(&decoder, offset));
    return ZX_OK;
  }

 private:
  Frobinator::GrobCallback callback_;

  Frobinator_Grob_ResponseHandler(const Frobinator_Grob_ResponseHandler&) =
      delete;
  Frobinator_Grob_ResponseHandler& operator=(
      const Frobinator_Grob_ResponseHandler&) = delete;
};

}  // namespace

void Frobinator_Proxy::Grob(::fidl::StringPtr value, GrobCallback callback) {
  using ResponseHandler = Frobinator_Grob_ResponseHandler;
  ::fidl::Encoder encoder(kFrobinator_Grob_Ordinal);
  size_t offset =
      encoder.Alloc(::fidl::CodingTraits<::fidl::StringPtr>::encoded_size);
  ::fidl::Encode(&encoder, &value, offset);
  controller_->Send(&unbounded_nonnullable_string_message_type,
                    encoder.GetMessage(),
                    std::make_unique<ResponseHandler>(std::move(callback)));
}

Frobinator_Stub::Frobinator_Stub(Frobinator* impl) : impl_(impl) {}

Frobinator_Stub::~Frobinator_Stub() = default;

namespace {

class Frobinator_Stub_Grob_Responder {
 public:
  Frobinator_Stub_Grob_Responder(::fidl::internal::PendingResponse response)
      : response_(std::move(response)) {}

  void operator()(::fidl::StringPtr value) {
    ::fidl::Encoder encoder(kFrobinator_Grob_Ordinal);
    size_t offset =
        encoder.Alloc(::fidl::CodingTraits<::fidl::StringPtr>::encoded_size);
    ::fidl::Encode(&encoder, &value, offset);
    response_.Send(&unbounded_nonnullable_string_message_type,
                   encoder.GetMessage());
  }

 private:
  ::fidl::internal::PendingResponse response_;
};

}  // namespace

zx_status_t Frobinator_Stub::Dispatch(
    ::fidl::Message message,
    ::fidl::internal::PendingResponse response) {
  zx_status_t status;
  switch (message.ordinal()) {
    case kFrobinator_Frob_Ordinal: {
      const char* error_msg = nullptr;
      status = message.Decode(&unbounded_nonnullable_string_message_type,
                              &error_msg);
      if (status != ZX_OK)
        break;
      ::fidl::Decoder decoder(std::move(message));
      size_t offset = sizeof(fidl_message_header_t);
      impl_->Frob(::fidl::DecodeAs<::fidl::StringPtr>(&decoder, offset));
      break;
    }
    case kFrobinator_Grob_Ordinal: {
      using Responder = Frobinator_Stub_Grob_Responder;
      const char* error_msg = nullptr;
      status = message.Decode(&unbounded_nonnullable_string_message_type,
                              &error_msg);
      if (status != ZX_OK)
        break;
      ::fidl::Decoder decoder(std::move(message));
      size_t offset = sizeof(fidl_message_header_t);
      impl_->Grob(::fidl::DecodeAs<::fidl::StringPtr>(&decoder, offset),
                  Responder(std::move(response)));
      break;
    }
    default: {
      status = ZX_ERR_NOT_SUPPORTED;
      break;
    }
  }
  return status;
}

Frobinator_SyncProxy::Frobinator_SyncProxy(zx::channel channel)
    : proxy_(::std::move(channel)) {}

Frobinator_SyncProxy::~Frobinator_SyncProxy() = default;

zx_status_t Frobinator_SyncProxy::Frob(::fidl::StringPtr value) {
  ::fidl::Encoder encoder(kFrobinator_Grob_Ordinal);
  size_t offset =
      encoder.Alloc(::fidl::CodingTraits<::fidl::StringPtr>::encoded_size);
  ::fidl::Encode(&encoder, &value, offset);
  return proxy_.Send(&unbounded_nonnullable_string_message_type,
                     encoder.GetMessage());
}

zx_status_t Frobinator_SyncProxy::Grob(::fidl::StringPtr value,
                                       ::fidl::StringPtr* out_result) {
  ::fidl::Encoder encoder(kFrobinator_Grob_Ordinal);
  size_t offset =
      encoder.Alloc(::fidl::CodingTraits<::fidl::StringPtr>::encoded_size);
  ::fidl::Encode(&encoder, &value, offset);
  ::fidl::MessageBuffer buffer;
  ::fidl::Message response = buffer.CreateEmptyMessage();
  zx_status_t status = proxy_.Call(&unbounded_nonnullable_string_message_type,
                                   &unbounded_nonnullable_string_message_type,
                                   encoder.GetMessage(), &response);
  if (status != ZX_OK)
    return status;
  ::fidl::Decoder decoder(std::move(response));
  offset = sizeof(fidl_message_header_t);
  *out_result = ::fidl::DecodeAs<::fidl::StringPtr>(&decoder, offset);
  return ZX_OK;
}

}  // namespace test
}  // namespace fidl
