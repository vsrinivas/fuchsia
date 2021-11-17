// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/fuchsia_http_client.h"

#include <fuchsia/net/http/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fsl/vmo/strings.h"

namespace cobalt {
namespace utils {

using lib::HTTPClient;
using lib::HTTPRequest;
using lib::HTTPResponse;
using lib::statusor::StatusOr;

namespace {

fuchsia::net::http::Request MakeRequest(const lib::HTTPRequest& request, zx::time deadline) {
  fuchsia::net::http::Request fx_request;
  fx_request.set_method("POST");
  fx_request.set_url(request.url);

  fsl::SizedVmo data;
  auto result = fsl::VmoFromString(request.body, &data);
  FX_CHECK(result);

  fx_request.mutable_body()->set_buffer(std::move(data).ToTransport());

  for (const auto& header : request.headers) {
    fuchsia::net::http::Header hdr;
    std::vector<uint8_t> name(header.first.begin(), header.first.end());
    hdr.name = name;

    std::vector<uint8_t> value(header.second.begin(), header.second.end());
    hdr.value = value;
    fx_request.mutable_headers()->push_back(std::move(hdr));
  }

  fx_request.set_deadline(deadline.get());
  return fx_request;
}

StatusOr<HTTPResponse> ReadResponse(fuchsia::net::http::Response fx_response, zx::time deadline) {
  HTTPResponse response;

  if (fx_response.has_error()) {
    std::ostringstream ss;
    ss << "Got error while making HTTP request: ";
    auto status_code = StatusCode::INTERNAL;
    switch (fx_response.error()) {
      case fuchsia::net::http::Error::INTERNAL:
        ss << "Internal Error";
        status_code = StatusCode::INTERNAL;
        break;
      case fuchsia::net::http::Error::UNABLE_TO_PARSE:
        ss << "Unable to parse HTTP data";
        status_code = StatusCode::INVALID_ARGUMENT;
        break;
      case fuchsia::net::http::Error::CHANNEL_CLOSED:
        ss << "Channel closed";
        status_code = StatusCode::ABORTED;
        break;
      case fuchsia::net::http::Error::CONNECT:
        ss << "Error occurred while connecting";
        status_code = StatusCode::UNAVAILABLE;
        break;
      case fuchsia::net::http::Error::DEADLINE_EXCEEDED:
        ss << "Deadline exceeded while waiting for response";
        status_code = StatusCode::DEADLINE_EXCEEDED;
        break;
    }
    return Status(status_code, ss.str());
  }

  response.http_code = fx_response.status_code();

  if (fx_response.has_headers()) {
    for (auto& header : fx_response.headers()) {
      std::string name(header.name.begin(), header.name.end());
      std::string value(header.value.begin(), header.value.end());
      response.headers.emplace(name, value);
    }
  }

  if (fx_response.has_body()) {
    std::vector<char> buffer(64 * 1024);
    size_t num_bytes = 0;
    bool more_data = true;
    while (more_data) {
      auto status = fx_response.mutable_body()->read(0, buffer.data(), buffer.size(), &num_bytes);
      switch (status) {
        case ZX_OK:
          response.response.append(static_cast<const char*>(buffer.data()), num_bytes);
          continue;
        case ZX_ERR_SHOULD_WAIT:
          // Wait until there is more to read.
          status = fx_response.mutable_body()->wait_one(
              ZX_SOCKET_READABLE | ZX_SOCKET_PEER_WRITE_DISABLED | ZX_SOCKET_PEER_CLOSED, deadline,
              /*observed_signals=*/nullptr);
          if (status != ZX_OK) {
            switch (status) {
              case ZX_ERR_TIMED_OUT:
                FX_LOGS(ERROR) << "Exceeded deadline while waiting on the socket";
                return Status(StatusCode::DEADLINE_EXCEEDED,
                              "Deadline exceeded while waiting on socket");
              default:
                FX_LOGS(ERROR) << "Unhandled zx_status_t: " << status;
                return Status(StatusCode::UNAVAILABLE, "Unhandled zx error");
            }
          }
          continue;
        case ZX_ERR_PEER_CLOSED:
          // The sending side of the channel has closed. There is no more data to read.
          more_data = false;
          break;
        default:
          FX_LOGS(ERROR) << "Unhandled zx_status_t: " << status;
          return Status(StatusCode::UNAVAILABLE, "Unhandled zx error");
      }
    }
  }

  return response;
}

}  // namespace

FuchsiaHTTPClient::FuchsiaHTTPClient(
    fit::function<::fuchsia::net::http::LoaderSyncPtr()> loader_factory)
    : loader_factory_(std::move(loader_factory)) {}

StatusOr<HTTPResponse> FuchsiaHTTPClient::PostSync(HTTPRequest request,
                                                   std::chrono::steady_clock::time_point deadline) {
  if (!loader_.is_bound()) {
    loader_ = loader_factory_();
  }

  auto zx_deadline =
      zx::deadline_after(zx::duration(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          deadline - std::chrono::steady_clock::now())
                                          .count()));

  fuchsia::net::http::Response fx_response;
  auto status = loader_->Fetch(MakeRequest(std::move(request), zx_deadline), &fx_response);

  if (status != ZX_OK) {
    std::ostringstream ss;
    ss << "Connection to HTTP Loader service lost, Perhaps it crashed? (ZX STATUS: "
       << zx_status_get_string(status) << ")";

    std::string s = ss.str();
    FX_LOGS(WARNING) << s;

    loader_.Unbind();
    return Status(StatusCode::UNAVAILABLE, s);
  }

  return ReadResponse(std::move(fx_response), zx_deadline);
}

}  // namespace utils
}  // namespace cobalt
