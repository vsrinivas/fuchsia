// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_UTILS_FUCHSIA_HTTP_CLIENT_H_
#define SRC_COBALT_BIN_UTILS_FUCHSIA_HTTP_CLIENT_H_

#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include "src/lib/network_wrapper/cancellable.h"
#include "src/lib/network_wrapper/network_wrapper.h"
#include "src/lib/fsl/socket/socket_drainer.h"
#include "src/lib/fxl/memory/ref_counted.h"
#include "third_party/cobalt/src/lib/clearcut/http_client.h"
#include "third_party/cobalt/src/lib/statusor/statusor.h"

namespace cobalt {
namespace utils {

class NetworkRequest;

// FuchsiaHTTPClient implements lib::clearcut::HTTPClient using fuchsia's
// NetworkWrapper library. Since this class uses the async_t supplied to the
// constructor to run all of the tasks on a single thread, this class is thread
// safe. However, the response from Post should not be waited on from that
// thread or a deadlock will occur.
class FuchsiaHTTPClient : public lib::clearcut::HTTPClient {
 public:
  FuchsiaHTTPClient(network_wrapper::NetworkWrapper* network_wrapper,
                    async_dispatcher_t* dispatcher);

  // Posts an HTTPRequest to fuchsia's network backend.
  //
  // Note: Do not invoke this method from |dispatcher_|'s thread.
  // Note: Do not wait on the returned future from |dispatcher_|'s thread.
  //
  // This FuchsiaHTTPClient instance must remain alive until the returned future
  // is completed.
  std::future<lib::statusor::StatusOr<lib::clearcut::HTTPResponse>> Post(
      lib::clearcut::HTTPRequest request, std::chrono::steady_clock::time_point deadline);

 private:
  virtual void HandleResponse(fxl::RefPtr<NetworkRequest> req,
                              ::fuchsia::net::oldhttp::URLResponse fx_response);
  virtual void HandleDeadline(fxl::RefPtr<NetworkRequest> req);
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
  NetworkRequest(lib::clearcut::HTTPRequest req) : request_(std::move(req)) {
    response_.response = "";
  }

  void ReadResponse(async_dispatcher_t* dispatcher, fxl::RefPtr<NetworkRequest> self,
                    zx::socket source);
  void OnDataAvailable(const void* data, size_t num_bytes);
  void OnDataComplete();

  void CancelCallbacks();

  std::future<lib::statusor::StatusOr<lib::clearcut::HTTPResponse>> get_future() {
    return promise_.get_future();
  }

  void SetValueAndCleanUp(lib::statusor::StatusOr<lib::clearcut::HTTPResponse> value);

  const lib::clearcut::HTTPRequest& request() { return request_; }

  void SetNetworkWrapperCancel(fxl::RefPtr<network_wrapper::Cancellable> network_wrapper_cancel) {
    network_wrapper_cancel_ = network_wrapper_cancel;
  }

  void SetDeadlineTask(std::unique_ptr<async::TaskClosure> deadline_task) {
    deadline_task_ = std::move(deadline_task);
  }

  void ScheduleDeadline(async_dispatcher_t* dispatcher, zx::duration duration) {
    deadline_task_->PostDelayed(dispatcher, duration);
  }

  lib::clearcut::HTTPResponse& response() { return response_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(NetworkRequest);
  ~NetworkRequest() {}

  // The Clearcut request.
  lib::clearcut::HTTPRequest request_;

  // The Clearcut response. This variable is populated during rsponse processing
  // in the case that the underlying network request succeeded, and then the
  // data is moved to |promise_|, leaving this variable empty, in
  // OnDataComplete().
  lib::clearcut::HTTPResponse response_;

  // The promise used for returning a value.
  std::promise<lib::statusor::StatusOr<lib::clearcut::HTTPResponse>> promise_;
  // A reference to itself that will be set when ReadResponse is used.
  fxl::RefPtr<NetworkRequest> self_;
  // Task which will cancel the network request if triggered.
  std::unique_ptr<async::TaskClosure> deadline_task_;
  // The callback to cancel the network request.
  fxl::RefPtr<network_wrapper::Cancellable> network_wrapper_cancel_;
  // The SocketDrainer used to read the data from the network
  std::unique_ptr<fsl::SocketDrainer> socket_drainer_;
};

}  // namespace utils
}  // namespace cobalt

#endif  // SRC_COBALT_BIN_UTILS_FUCHSIA_HTTP_CLIENT_H_
