// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/net/oldhttp/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>

#include "garnet/bin/cobalt/utils/fuchsia_http_client.h"
#include "lib/fsl/socket/socket_drainer.h"
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

class NetworkRequest : public fxl::RefCountedThreadSafe<NetworkRequest>,
                       public fsl::SocketDrainer::Client {
 public:
  // The HTTPRequest to send to network_manager.
  HTTPRequest request;
  // Task which will cancel the network request if triggered.
  std::unique_ptr<async::TaskClosure> deadline_task;
  // The callback to cancel the network request.
  fxl::RefPtr<callback::Cancellable> network_wrapper_cancel;
  // The SocketDrainer used to read the data from the network
  std::unique_ptr<fsl::SocketDrainer> socket_drainer;

  NetworkRequest() {}

  void ReadResponse(async_t* async, fxl::RefPtr<NetworkRequest> self,
                    uint32_t http_code, zx::socket source) {
    self_ = self;
    http_code_ = http_code;
    socket_drainer.reset(new fsl::SocketDrainer(this, async));
    socket_drainer->Start(std::move(source));
  }

  void OnDataAvailable(const void* data, size_t num_bytes) {
    response_.append(static_cast<const char*>(data), num_bytes);
  }

  void OnDataComplete() {
    HTTPResponse response;
    response.response = response_;
    response.http_code = http_code_;
    set_value(response);
    self_ = nullptr;
  }

  std::future<StatusOr<HTTPResponse>> get_future() {
    return promise_.get_future();
  }

  void set_value(StatusOr<HTTPResponse> value) { promise_.set_value(value); }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(NetworkRequest);
  ~NetworkRequest() {}

  std::string response_;
  uint32_t http_code_;
  // The promise used for returning a value.
  std::promise<StatusOr<HTTPResponse>> promise_;
  // A reference to itself that will be set when ReadResponse is used.
  fxl::RefPtr<NetworkRequest> self_;
};

}  // namespace

FuchsiaHTTPClient::FuchsiaHTTPClient(
    network_wrapper::NetworkWrapper* network_wrapper, async_t* async)
    : network_wrapper_(network_wrapper), async_(async) {}

std::future<StatusOr<HTTPResponse>> FuchsiaHTTPClient::Post(
    HTTPRequest request, std::chrono::steady_clock::time_point deadline) {
  ZX_ASSERT_MSG(async_get_default() != async_,
                "Post should not be called from the same thread as async_, as "
                "this may cause deadlocks");
  auto network_request = fxl::MakeRefCounted<NetworkRequest>();
  network_request->deadline_task.reset(new async::TaskClosure());
  network_request->request = std::move(request);

  network_request->deadline_task->set_handler([req = network_request] {
    // network_wrapper_cancel should always be set at this point.
    FXL_DCHECK(req->network_wrapper_cancel);
    req->network_wrapper_cancel->Cancel();

    req->set_value(
        util::Status(util::StatusCode::DEADLINE_EXCEEDED,
                     "Deadline exceeded while waiting for network request"));
  });

  auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      deadline - std::chrono::steady_clock::now())
                      .count();
  network_request->deadline_task->PostDelayed(async_, zx::nsec(duration));

  async::PostTask(async_, [this, req = network_request] {
    req->network_wrapper_cancel = network_wrapper_->Request(
        [req]() {
          http::URLRequest fx_request;
          fx_request.url = req->request.url;
          fx_request.method = "POST";
          fx_request.auto_follow_redirects = true;
          fx_request.body = http::URLBody::New();

          fsl::SizedVmo data;
          auto result = fsl::VmoFromString(req->request.body, &data);
          FXL_CHECK(result);

          fx_request.body->set_sized_buffer(std::move(data).ToTransport());
          for (const auto& header : req->request.headers) {
            http::HttpHeader hdr;
            hdr.name = header.first;
            hdr.value = header.second;
            fx_request.headers.push_back(std::move(hdr));
          }
          return fx_request;
        },
        [this, req](http::URLResponse fx_response) {
          FXL_DCHECK(req->deadline_task);
          req->deadline_task->Cancel();
          req->deadline_task.reset(nullptr);
          if (fx_response.error) {
            std::ostringstream ss;
            ss << fx_response.url << " error "
               << fx_response.error->description;
            req->set_value(util::Status(util::StatusCode::INTERNAL, ss.str()));
            return;
          }
          if (fx_response.body) {
            FXL_DCHECK(fx_response.body->is_stream());
            req->ReadResponse(async_, req, fx_response.status_code,
                              std::move(fx_response.body->stream()));
          } else {
            HTTPResponse response;
            response.response = "";
            response.http_code = fx_response.status_code;
            req->set_value(response);
          }
        });
  });
  return network_request->get_future();
}

}  // namespace utils
}  // namespace cobalt
