// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/bindings2/test/frobinator.h"

#include <zircon/assert.h>

#include <memory>

#include "lib/fidl/cpp/bindings2/test/fidl_types.h"

namespace fidl {
namespace test {
namespace {

constexpr uint32_t kFrobinator_Frob_Ordinal = 1u;
constexpr uint32_t kFrobinator_Grob_Ordinal = 2u;

}  // namespace

Frobinator::~Frobinator() = default;

FrobinatorProxy::FrobinatorProxy(::fidl::internal::ProxyController* controller)
    : controller_(controller) {}

FrobinatorProxy::~FrobinatorProxy() = default;

void FrobinatorProxy::Frob(::fidl::StringPtr value) {
  ::fidl::MessageBuilder builder(&unbounded_nonnullable_string_message_type);
  builder.header()->ordinal = kFrobinator_Frob_Ordinal;
  ::fidl::Build(&builder, value);
  controller_->Send(&builder, nullptr);
}

namespace {

class Frobinator_Grob_ResponseHandler
    : public ::fidl::internal::MessageHandler {
 public:
  Frobinator_Grob_ResponseHandler(FrobinatorProxy::GrobCallback callback)
      : callback_(std::move(callback)) {
    ZX_DEBUG_ASSERT_MSG(callback_,
                        "Callback must not be empty for Frobinator.Grob\n");
  }

  zx_status_t OnMessage(::fidl::Message message) override {
    using Params = ::fidl::StringPtr;
    const char* error_msg = nullptr;
    zx_status_t status =
        message.Decode(&unbounded_nonnullable_string_message_type, &error_msg);
    if (status != ZX_OK)
      return status;
    callback_(Params::Take(message.GetPayloadAs<Params::View>()));
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

void FrobinatorProxy::Grob(::fidl::StringPtr value, GrobCallback callback) {
  using ResponseHandler = Frobinator_Grob_ResponseHandler;
  ::fidl::MessageBuilder builder(&unbounded_nonnullable_string_message_type);
  builder.header()->ordinal = kFrobinator_Grob_Ordinal;
  ::fidl::Build(&builder, value);
  controller_->Send(&builder,
                    std::make_unique<ResponseHandler>(std::move(callback)));
}

FrobinatorStub::FrobinatorStub(Frobinator* impl) : impl_(impl) {}

FrobinatorStub::~FrobinatorStub() = default;

namespace {

class FrobinatorStub_Grob_Responder {
 public:
  FrobinatorStub_Grob_Responder(::fidl::internal::PendingResponse response)
      : response_(std::move(response)) {}

  void operator()(::fidl::StringPtr value) {
    ::fidl::MessageBuilder builder(&unbounded_nonnullable_string_message_type);
    builder.header()->ordinal = kFrobinator_Grob_Ordinal;
    ::fidl::Build(&builder, value);
    response_.Send(&builder);
  }

 private:
  ::fidl::internal::PendingResponse response_;
};

}  // namespace

zx_status_t FrobinatorStub::Dispatch(
    ::fidl::Message message,
    ::fidl::internal::PendingResponse response) {
  zx_status_t status;
  switch (message.ordinal()) {
    case kFrobinator_Frob_Ordinal: {
      using Params = ::fidl::StringPtr;
      const char* error_msg = nullptr;
      status = message.Decode(&unbounded_nonnullable_string_message_type,
                              &error_msg);
      if (status != ZX_OK)
        break;
      impl_->Frob(Params::Take(message.GetPayloadAs<Params::View>()));
      break;
    }
    case kFrobinator_Grob_Ordinal: {
      using Params = ::fidl::StringPtr;
      using Responder = FrobinatorStub_Grob_Responder;
      const char* error_msg = nullptr;
      status = message.Decode(&unbounded_nonnullable_string_message_type,
                              &error_msg);
      if (status != ZX_OK)
        break;
      impl_->Grob(Params::Take(message.GetPayloadAs<Params::View>()),
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

}  // namespace test
}  // namespace fidl
