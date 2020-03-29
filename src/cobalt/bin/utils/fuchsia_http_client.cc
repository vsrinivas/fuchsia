// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/utils/fuchsia_http_client.h"

#include <fuchsia/net/http/cpp/fidl.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <zircon/status.h>

#include "src/lib/fsl/socket/strings.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/syslog/cpp/logger.h"

namespace cobalt {
namespace utils {

using lib::clearcut::HTTPClient;
using lib::clearcut::HTTPRequest;
using lib::clearcut::HTTPResponse;
using lib::statusor::StatusOr;

namespace {

fuchsia::net::http::Request MakeRequest(const lib::clearcut::HTTPRequest& request) {
  fuchsia::net::http::Request fx_request;
  fx_request.set_method("POST");
  fx_request.set_url(request.url);

  fsl::SizedVmo data;
  auto result = fsl::VmoFromString(request.body, &data);
  FXL_CHECK(result);

  fx_request.mutable_body()->set_buffer(std::move(data).ToTransport());

  for (const auto& header : request.headers) {
    fuchsia::net::http::Header hdr;
    std::vector<uint8_t> name(header.first.begin(), header.first.end());
    hdr.name = name;

    std::vector<uint8_t> value(header.second.begin(), header.second.end());
    hdr.value = value;
    fx_request.mutable_headers()->push_back(std::move(hdr));
  }
  return fx_request;
}

}  // namespace

FuchsiaHTTPClient::FuchsiaHTTPClient(
    async_dispatcher_t* dispatcher, fit::function<::fuchsia::net::http::LoaderPtr()> loader_factory)
    : dispatcher_(dispatcher),
      loader_factory_(std::move(loader_factory)),
      socket_drainer_(this, dispatcher_),
      deadline_task_([this] {
        SetValue(util::Status(util::StatusCode::DEADLINE_EXCEEDED,
                              "Deadline exceeded while waiting for network request"));
      }),
      task_runner_(dispatcher_) {
  FXL_CHECK(dispatcher_);
  ConnectToLoader();
}

StatusOr<HTTPResponse> FuchsiaHTTPClient::PostSync(HTTPRequest request,
                                                   std::chrono::steady_clock::time_point deadline) {
  FXL_CHECK(async_get_default_dispatcher() != dispatcher_)
      << "PostSync should not be called from the same thread as dispatcher_, as this will cause "
         "deadlocks";

  std::promise<lib::statusor::StatusOr<lib::clearcut::HTTPResponse>> promise;
  auto result_future = promise.get_future();

  task_runner_.PostTask(
      [this, request = std::move(request), deadline, promise = std::move(promise)]() mutable {
        Start(std::move(request), deadline, std::move(promise));
      });

  return result_future.get();
}

void FuchsiaHTTPClient::Start(
    lib::clearcut::HTTPRequest request, std::chrono::steady_clock::time_point deadline,
    std::promise<lib::statusor::StatusOr<lib::clearcut::HTTPResponse>> promise) {
  FXL_CHECK(async_get_default_dispatcher() == dispatcher_)
      << "FuchsiaHTTPClient::Start must be run on the dispatcher_ thread.";

  if (active_request_ != nullptr) {
    promise.set_value(
        util::Status(util::StatusCode::UNAVAILABLE, "A request is already in progress"));
    return;
  }

  active_request_ =
      std::make_unique<FuchsiaHTTPClient::ActiveRequest>(std::move(request), std::move(promise));

  // Schedule the deadline
  deadline_task_.PostDelayed(dispatcher_,
                             zx::nsec(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                          deadline - std::chrono::steady_clock::now())
                                          .count()));

  ConnectToLoader();

  loader_->Fetch(MakeRequest(active_request_->request),
                 [this, request_id = ++current_request_](fuchsia::net::http::Response fx_response) {
                   HandleResponse(request_id, std::move(fx_response));
                 });
}

void FuchsiaHTTPClient::ConnectToLoader() {
  if (!loader_.is_bound()) {
    loader_ = loader_factory_();
    loader_.set_error_handler([this](zx_status_t status) {
      std::ostringstream ss;
      ss << "Connection to HTTP Loader service lost, Perhaps it crashed? (ZX STATUS: "
         << zx_status_get_string(status) << ")";
      FX_LOGS(WARNING) << ss.str();

      loader_.Unbind();
      SetValue(util::Status(util::StatusCode::UNAVAILABLE, ss.str()));
    });
  }
}

void FuchsiaHTTPClient::HandleResponse(int request_id, fuchsia::net::http::Response fx_response) {
  if (!active_request_ || current_request_ != request_id) {
    FX_LOGS(WARNING) << "Got response for non-active request (For request = " << request_id
                     << ", current_request = " << current_request_ << ")";
    return;
  }

  CancelDeadline();
  if (fx_response.has_error()) {
    std::ostringstream ss;
    ss << "Got error while making running_requestuest: ";
    auto status_code = util::StatusCode::INTERNAL;
    switch (fx_response.error()) {
      case fuchsia::net::http::Error::INTERNAL:
        ss << "Internal Error";
        status_code = util::StatusCode::INTERNAL;
        break;
      case fuchsia::net::http::Error::UNABLE_TO_PARSE:
        ss << "Unable to parse HTTP data";
        status_code = util::StatusCode::INVALID_ARGUMENT;
        break;
      case fuchsia::net::http::Error::CHANNEL_CLOSED:
        ss << "Channel closed";
        status_code = util::StatusCode::ABORTED;
        break;
      case fuchsia::net::http::Error::CONNECT:
        ss << "Error occurred while connecting";
        status_code = util::StatusCode::UNAVAILABLE;
        break;
      case fuchsia::net::http::Error::DEADLINE_EXCEEDED:
        ss << "Deadline exceeded while waiting for response";
        status_code = util::StatusCode::DEADLINE_EXCEEDED;
        break;
    }
    SetValue(util::Status(status_code, ss.str()));
    return;
  }
  active_request_->response.http_code = fx_response.status_code();

  if (fx_response.has_headers()) {
    for (auto& header : fx_response.headers()) {
      std::string name(header.name.begin(), header.name.end());
      std::string value(header.value.begin(), header.value.end());
      active_request_->response.headers.emplace(name, value);
    }
  }

  if (fx_response.has_body()) {
    socket_drainer_.Start(std::move(*fx_response.mutable_body()));
  } else {
    OnDataComplete();
  }
}

void FuchsiaHTTPClient::SetValue(StatusOr<HTTPResponse> value) {
  CancelDeadline();

  if (active_request_) {
    active_request_->promise.set_value(std::move(value));
    active_request_ = nullptr;
  }
}

void FuchsiaHTTPClient::OnDataAvailable(const void* data, size_t num_bytes) {
  if (active_request_) {
    FX_VLOGS(6) << "FuchsiaHTTPClient::OnDataAvailable(" << num_bytes << ")";
    active_request_->response.response.append(static_cast<const char*>(data), num_bytes);
  }
}

void FuchsiaHTTPClient::OnDataComplete() {
  if (active_request_) {
    FX_VLOGS(6) << "FuchsiaHTTPClient::OnDataComplete()";
    SetValue(std::move(active_request_->response));
  }
}

}  // namespace utils
}  // namespace cobalt
