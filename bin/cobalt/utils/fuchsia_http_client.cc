// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>

#include "garnet/bin/cobalt/utils/fuchsia_http_client.h"
#include "lib/fsl/socket/strings.h"
#include "lib/fsl/vmo/strings.h"
#include "lib/fxl/memory/ref_counted.h"

namespace cobalt {
namespace utils {

namespace http = ::fuchsia::net::oldhttp;

using clearcut::HTTPClient;
using clearcut::HTTPRequest;
using clearcut::HTTPResponse;
using tensorflow_statusor::StatusOr;

namespace {

http::URLRequest MakeRequest(fxl::RefPtr<NetworkRequest> network_request) {
  http::URLRequest fx_request;
  fx_request.url = network_request->request().url;
  fx_request.method = "POST";
  fx_request.auto_follow_redirects = true;
  fx_request.body = http::URLBody::New();

  fsl::SizedVmo data;
  auto result = fsl::VmoFromString(network_request->request().body, &data);
  FXL_CHECK(result);

  fx_request.body->set_sized_buffer(std::move(data).ToTransport());
  for (const auto& header : network_request->request().headers) {
    http::HttpHeader hdr;
    hdr.name = header.first;
    hdr.value = header.second;
    fx_request.headers.push_back(std::move(hdr));
  }
  return fx_request;
}

}  // namespace

void NetworkRequest::ReadResponse(async_dispatcher_t* dispatcher,
                                  fxl::RefPtr<NetworkRequest> self,
                                  uint32_t http_code, zx::socket source) {
  // Store a reference to myself, so that I don't get deleted while reading from
  // the socket.
  self_ = self;
  http_code_ = http_code;
  socket_drainer_ = std::make_unique<fsl::SocketDrainer>(this, dispatcher);
  socket_drainer_->Start(std::move(source));
}

void NetworkRequest::OnDataAvailable(const void* data, size_t num_bytes) {
  response_.append(static_cast<const char*>(data), num_bytes);
}

void NetworkRequest::OnDataComplete() {
  HTTPResponse response;
  response.response = response_;
  response.http_code = http_code_;
  SetValueAndCleanUp(std::move(response));
}

FuchsiaHTTPClient::FuchsiaHTTPClient(
    network_wrapper::NetworkWrapper* network_wrapper, async_dispatcher_t* dispatcher)
    : network_wrapper_(network_wrapper), dispatcher_(dispatcher) {}

void FuchsiaHTTPClient::HandleResponse(fxl::RefPtr<NetworkRequest> req,
                                       http::URLResponse fx_response) {
  req->CancelCallbacks();
  if (fx_response.error) {
    std::ostringstream ss;
    ss << fx_response.url << " error " << fx_response.error->description;
    req->SetValueAndCleanUp(util::Status(util::StatusCode::INTERNAL, ss.str()));
    return;
  }
  if (fx_response.body) {
    FXL_DCHECK(fx_response.body->is_stream());
    req->ReadResponse(dispatcher_, req, fx_response.status_code,
                      std::move(fx_response.body->stream()));
  } else {
    HTTPResponse response;
    response.response = "";
    response.http_code = fx_response.status_code;
    req->SetValueAndCleanUp(std::move(response));
  }
}

void FuchsiaHTTPClient::HandleDeadline(fxl::RefPtr<NetworkRequest> req) {
  req->CancelCallbacks();
  req->SetValueAndCleanUp(
      util::Status(util::StatusCode::DEADLINE_EXCEEDED,
                   "Deadline exceeded while waiting for network request"));
}

void FuchsiaHTTPClient::SendRequest(
    fxl::RefPtr<NetworkRequest> network_request) {
  network_request->SetNetworkWrapperCancel(network_wrapper_->Request(
      std::bind(&MakeRequest, network_request),
      [this, network_request](http::URLResponse fx_response) {
        HandleResponse(network_request, std::move(fx_response));
      }));
}

void NetworkRequest::CancelCallbacks() {
  if (network_wrapper_cancel_) {
    network_wrapper_cancel_->Cancel();
  }
  if (deadline_task_) {
    deadline_task_->Cancel();
  }
}

void NetworkRequest::SetValueAndCleanUp(StatusOr<HTTPResponse> value) {
  promise_.set_value(std::move(value));

  // Clean up stored references so NetworkRequest can be freed.
  if (network_wrapper_cancel_) {
    network_wrapper_cancel_ = nullptr;
  }
  if (deadline_task_) {
    deadline_task_ = nullptr;
  }
  if (socket_drainer_) {
    socket_drainer_ = nullptr;
  }
  self_ = nullptr;
}

std::future<StatusOr<HTTPResponse>> FuchsiaHTTPClient::Post(
    HTTPRequest request, std::chrono::steady_clock::time_point deadline) {
  ZX_ASSERT_MSG(async_get_default_dispatcher() != dispatcher_,
                "Post should not be called from the same thread as dispatcher_, as "
                "this may cause deadlocks");
  auto network_request =
      fxl::MakeRefCounted<NetworkRequest>(std::move(request));
  network_request->SetDeadlineTask(std::make_unique<async::TaskClosure>(
      [this, network_request] { HandleDeadline(network_request); }));

  async::PostTask(dispatcher_,
                  [this, network_request]() { SendRequest(network_request); });

  auto duration = zx::nsec(std::chrono::duration_cast<std::chrono::nanoseconds>(
                               deadline - std::chrono::steady_clock::now())
                               .count());
  network_request->ScheduleDeadline(dispatcher_, duration);

  return network_request->get_future();
}

}  // namespace utils
}  // namespace cobalt
