// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_UTILS_FUCHSIA_HTTP_CLIENT_H_
#define SRC_COBALT_BIN_UTILS_FUCHSIA_HTTP_CLIENT_H_

#include <fuchsia/net/http/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>

#include "src/lib/callback/scoped_task_runner.h"
#include "src/lib/fsl/socket/socket_drainer.h"
#include "src/lib/syslog/cpp/logger.h"
#include "third_party/cobalt/src/lib/clearcut/http_client.h"
#include "third_party/cobalt/src/lib/statusor/statusor.h"

namespace cobalt {
namespace utils {

// FuchsiaHTTPClient implements lib::clearcut::HTTPClient using fuchsia's fuchsia::net::http::Loader
// FIDL interface. This class should only be used from one thread, and should not be run on the
// thread associated with the async_dispatcher_t provided to the class.
class FuchsiaHTTPClient : public lib::clearcut::HTTPClient, public fsl::SocketDrainer::Client {
 public:
  using LoaderFactory = fit::function<::fuchsia::net::http::LoaderPtr()>;
  using Response = lib::statusor::StatusOr<lib::clearcut::HTTPResponse>;

  FuchsiaHTTPClient(async_dispatcher_t* dispatcher, LoaderFactory loader_factory);

  // Posts an HTTPRequest to fuchsia's network backend.
  //
  // Note: This method is not thread-safe. Do not invoke this method concurrently from two threads.
  //       Furthermore, do not invoke this method from |dispatcher_|'s thread. Doing so will result
  //       in deadlock.
  Response PostSync(lib::clearcut::HTTPRequest request,
                    std::chrono::steady_clock::time_point deadline) override;

 private:
  // N.B. None of the following methods should be called from outside the dispatcher_ thread.

  // Start begins the process of making the HTTP request.
  //
  // |request|  The request information
  // |deadline| A time_point at which this request should time out if not otherwise resolved.
  // |promise|  The promise that will be used to return the result to the calling thread. (The
  //            caller should keep this promise's future in order to get the result)
  void Start(lib::clearcut::HTTPRequest request, std::chrono::steady_clock::time_point deadline,
             std::promise<Response> promise);

  // ConnectToLoader attempts to connect to the fuchsia::net::http::Loader service if it is not
  // already connected. It uses the |loader_factory| from the FuchsiaHTTPClient constructor.
  void ConnectToLoader();

  // HandleResponse takes the response from fuchsia::net::http::Loader, and converts it into a
  // lib::clearcut::HTTPResponse. If there is a body to read, this will start reading from it using
  // |socket_drainer_|.
  void HandleResponse(int request_id, ::fuchsia::net::http::Response fx_response);

  // SetValue cancels the outstanding deadline task, and sets the provided |value| on the
  // active_request's promise.
  void SetValue(lib::statusor::StatusOr<lib::clearcut::HTTPResponse> value);

  // These two functions implement fsl::SocketDrainer::Client
  void OnDataAvailable(const void* data, size_t num_bytes) override;
  void OnDataComplete() override;

  // CancelDeadline cancels the deadline task.
  void CancelDeadline() { deadline_task_.Cancel(); }

  async_dispatcher_t* dispatcher_;

  // N.B. None of the following fields should be accessed outside of the dispatcher_ thread.
  const LoaderFactory loader_factory_;
  ::fuchsia::net::http::LoaderPtr loader_;

  // The SocketDrainer used to read the data from the network
  fsl::SocketDrainer socket_drainer_;

  // Task which will cancel the network request if triggered.
  async::TaskClosure deadline_task_;

  callback::ScopedTaskRunner task_runner_;

  int current_request_ = 0;

  struct ActiveRequest {
    ActiveRequest(lib::clearcut::HTTPRequest request, std::promise<Response> promise)
        : request(std::move(request)), promise(std::move(promise)) {}

    // The Clearcut request.
    lib::clearcut::HTTPRequest request;

    // The promise used for returning a value.
    std::promise<Response> promise;

    // The Clearcut response. This variable is populated during rsponse processing
    // in the case that the underlying network request succeeded, and then the
    // data is moved to |promise_|, leaving this variable empty, in
    // OnDataComplete().
    lib::clearcut::HTTPResponse response;
  };

  std::unique_ptr<ActiveRequest> active_request_;
};

}  // namespace utils
}  // namespace cobalt

#endif  // SRC_COBALT_BIN_UTILS_FUCHSIA_HTTP_CLIENT_H_
