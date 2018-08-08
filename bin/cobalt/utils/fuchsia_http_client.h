// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_UTILS_FUCHSIA_HTTP_CLIENT_H_
#define GARNET_BIN_COBALT_UTILS_FUCHSIA_HTTP_CLIENT_H_

#include "garnet/public/lib/network_wrapper/network_wrapper.h"
#include "lib/fsl/socket/socket_drainer.h"
#include "third_party/cobalt/third_party/clearcut/http_client.h"
#include "third_party/cobalt/third_party/tensorflow_statusor/statusor.h"

namespace cobalt {
namespace utils {

class NetworkRequest;

// FuchsiaHTTPClient implements clearcut::HTTPClient using fuchsia's
// NetworkWrapper library. Since this class uses the async_t supplied to the
// constructor to run all of the tasks on a single thread, this class is thread
// safe. However, the response from Post should not be waited on from that
// thread or a deadlock will occur.
class FuchsiaHTTPClient : public ::clearcut::HTTPClient {
 public:
  FuchsiaHTTPClient(network_wrapper::NetworkWrapper* network_wrapper,
                    async_dispatcher_t* dispatcher);

  // Posts an HTTPRequest to fuchsia's network backend.
  //
  // Note: Do not invoke this method from |dispatcher_|'s thread.
  // Note: Do not wait on the returned future from |dispatcher_|'s thread.
  std::future<tensorflow_statusor::StatusOr<clearcut::HTTPResponse>> Post(
      clearcut::HTTPRequest request,
      std::chrono::steady_clock::time_point deadline);

 protected:
  // These are internal only functions that are intended to make
  // instrumentation of tests easier.
  virtual void HandleResponse(fxl::RefPtr<NetworkRequest> req,
                              ::fuchsia::net::oldhttp::URLResponse fx_response);
  virtual void HandleDeadline(fxl::RefPtr<NetworkRequest> req);

 private:
  virtual void SendRequest(fxl::RefPtr<NetworkRequest> network_request);

  // |network_wrapper_| is thread averse, and should only be accessed on the
  // main thread of |dispatcher_|.
  network_wrapper::NetworkWrapper* network_wrapper_;
  async_dispatcher_t* dispatcher_;
};

// NetworkRequest holds the state information for a single call to
// FuchsiaHTTPClient::Post.
class NetworkRequest : public fxl::RefCountedThreadSafe<NetworkRequest>,
                       public fsl::SocketDrainer::Client {
 public:
  NetworkRequest(clearcut::HTTPRequest req) : request_(std::move(req)) {}

  void ReadResponse(async_dispatcher_t* dispatcher,
                    fxl::RefPtr<NetworkRequest> self, uint32_t http_code,
                    zx::socket source);
  void OnDataAvailable(const void* data, size_t num_bytes);
  void OnDataComplete();

  void CancelCallbacks();

  std::future<tensorflow_statusor::StatusOr<clearcut::HTTPResponse>>
  get_future() {
    return promise_.get_future();
  }

  void SetValueAndCleanUp(
      tensorflow_statusor::StatusOr<clearcut::HTTPResponse> value);

  const clearcut::HTTPRequest& request() { return request_; }

  void SetNetworkWrapperCancel(
      fxl::RefPtr<callback::Cancellable> network_wrapper_cancel) {
    network_wrapper_cancel_ = network_wrapper_cancel;
  }

  void SetDeadlineTask(std::unique_ptr<async::TaskClosure> deadline_task) {
    deadline_task_ = std::move(deadline_task);
  }

  void ScheduleDeadline(async_dispatcher_t* dispatcher, zx::duration duration) {
    deadline_task_->PostDelayed(dispatcher, duration);
  }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(NetworkRequest);
  ~NetworkRequest() {}

  // The request object.
  clearcut::HTTPRequest request_;
  // Response information to be sent to the promise.
  std::string response_;
  uint32_t http_code_;
  // The promise used for returning a value.
  std::promise<tensorflow_statusor::StatusOr<clearcut::HTTPResponse>> promise_;
  // A reference to itself that will be set when ReadResponse is used.
  fxl::RefPtr<NetworkRequest> self_;
  // Task which will cancel the network request if triggered.
  std::unique_ptr<async::TaskClosure> deadline_task_;
  // The callback to cancel the network request.
  fxl::RefPtr<callback::Cancellable> network_wrapper_cancel_;
  // The SocketDrainer used to read the data from the network
  std::unique_ptr<fsl::SocketDrainer> socket_drainer_;
};

}  // namespace utils
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_UTILS_FUCHSIA_HTTP_CLIENT_H_
