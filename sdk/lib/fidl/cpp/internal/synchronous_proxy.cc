// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fidl/cpp/internal/synchronous_proxy.h"

#include <memory>
#include <utility>

#include "lib/fidl/cpp/internal/logging.h"
#include "lib/fidl/cpp/internal/proxy_controller_util.h"

namespace fidl {
namespace internal {

SynchronousProxy::SynchronousProxy(zx::channel channel) : channel_(std::move(channel)) {}

SynchronousProxy::~SynchronousProxy() = default;

zx::channel SynchronousProxy::TakeChannel() { return std::move(channel_); }

zx_status_t SynchronousProxy::Send(const fidl_type_t* type, Message message) {
  return fidl::internal::SendMessage(channel_, type, std::move(message));
}

zx_status_t SynchronousProxy::Call(const fidl_type_t* request_type,
                                   const fidl_type_t* response_type, Message request,
                                   Message* response) {
  const char* error_msg = nullptr;
  auto header = request.header();
  if (!fidl_should_decode_union_from_xunion(&header)) {
    zx_status_t status = request.Validate(request_type, &error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_ENCODING_ERROR(request, request_type, error_msg);
      return status;
    }
  } else {
    zx_status_t status = ValidateV1Bytes(request_type, request, error_msg);
    if (status != ZX_OK) {
      FIDL_REPORT_ENCODING_ERROR(request, request_type, error_msg);
      return status;
    }
  }
  zx_status_t status = request.Call(channel_.get(), 0, ZX_TIME_INFINITE, response);
  if (status != ZX_OK)
    return status;
  status = response->Decode(response_type, &error_msg);
  if (status != ZX_OK) {
    FIDL_REPORT_DECODING_ERROR(*response, response_type, error_msg);
    return status;
  }
  return ZX_OK;
}

}  // namespace internal
}  // namespace fidl
