// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/forensics/testing/stubs/loader.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/socket.h>

#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace forensics {
namespace stubs {

using namespace fuchsia::net::http;

LoaderResponse LoaderResponse::WithError(const Error error) {
  return LoaderResponse{
      .error = error,
      .status_code = std::nullopt,
      .body = std::nullopt,
  };
}

LoaderResponse LoaderResponse::WithError(const uint32_t status_code) {
  FX_CHECK(status_code < 200 || status_code >= 204);

  return LoaderResponse{
      .error = std::nullopt,
      .status_code = status_code,
      .body = std::nullopt,
  };
}

LoaderResponse LoaderResponse::WithBody(const uint32_t status_code, const std::string& body) {
  return LoaderResponse{
      .error = std::nullopt,
      .status_code = status_code,
      .body = body,
  };
}

Loader::~Loader() {
  FX_CHECK(next_response_ == responses_.end())
      << fxl::StringPrintf("expected %ld more calls to Fetch() (%ld/%lu calls made)",
                           std::distance(next_response_, responses_.cend()),
                           std::distance(responses_.cbegin(), next_response_), responses_.size());
}

void Loader::Fetch(Request request, FetchCallback callback) {
  FX_CHECK(next_response_ != responses_.end())
      << fxl::StringPrintf("no more calls to Fetch() expected (%lu/%lu calls made)",
                           std::distance(responses_.cbegin(), next_response_), responses_.size());
  FX_CHECK(next_response_ != responses_.end());

  Response response;
  if (next_response_->error.has_value()) {
    response.set_error(next_response_->error.value());
  } else if (next_response_->status_code.has_value()) {
    response.set_status_code(next_response_->status_code.value());
    if (next_response_->body.has_value()) {
      response.set_body(fsl::WriteStringToSocket(next_response_->body.value()));
    }
  } else {
    FX_LOGS(FATAL) << "Bad LoaderResponse";
  }

  next_response_++;
  callback(std::move(response));
}

}  // namespace stubs
}  // namespace forensics
