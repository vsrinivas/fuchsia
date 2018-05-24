// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_COBALT_UTILS_FUCHSIA_HTTP_CLIENT_H_
#define GARNET_BIN_COBALT_UTILS_FUCHSIA_HTTP_CLIENT_H_

#include "garnet/public/lib/network_wrapper/network_wrapper.h"
#include "third_party/cobalt/third_party/clearcut/http_client.h"
#include "third_party/cobalt/third_party/tensorflow_statusor/statusor.h"

namespace cobalt {
namespace utils {

// FuchsiaHTTPClient implements clearcut::HTTPClient using fuchsia's
// NetworkWrapper library. Since this class uses the async_t supplied to the
// constructor to run all of the tasks on a single thread, this class is thread
// safe. However, the response from Post should not be waited on from that
// thread or a deadlock will occur.
class FuchsiaHTTPClient : public ::clearcut::HTTPClient {
 public:
  FuchsiaHTTPClient(network_wrapper::NetworkWrapper* network_wrapper,
                    async_t* async);

  // Posts an HTTPRequest to fuchsia's network backend.
  //
  // Note: Do not invoke this method from |async_|'s thread.
  // Note: Do not wait on the returned future from |async_|'s thread.
  std::future<tensorflow_statusor::StatusOr<::clearcut::HTTPResponse>> Post(
      ::clearcut::HTTPRequest request,
      std::chrono::steady_clock::time_point deadline);

 private:
  // |network_wrapper_| is thread averse, and should only be accessed on the
  // main thread of |async_|.
  network_wrapper::NetworkWrapper* network_wrapper_;
  async_t* async_;
};

}  // namespace utils
}  // namespace cobalt

#endif  // GARNET_BIN_COBALT_UTILS_FUCHSIA_HTTP_CLIENT_H_
