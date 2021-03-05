// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_COBALT_BIN_UTILS_FUCHSIA_HTTP_CLIENT_H_
#define SRC_COBALT_BIN_UTILS_FUCHSIA_HTTP_CLIENT_H_

#include <fuchsia/net/http/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/dispatcher.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/callback/scoped_task_runner.h"
#include "third_party/cobalt/src/lib/statusor/statusor.h"
#include "third_party/cobalt/src/public/lib/http_client.h"

namespace cobalt {
namespace utils {

// FuchsiaHTTPClient implements lib::HTTPClient using fuchsia's fuchsia::net::http::Loader
// FIDL interface.
class FuchsiaHTTPClient : public lib::HTTPClient {
 public:
  using LoaderFactory = fit::function<::fuchsia::net::http::LoaderSyncPtr()>;

  FuchsiaHTTPClient(LoaderFactory loader_factory);

  // Posts an HTTPRequest to fuchsia's network backend.
  lib::statusor::StatusOr<lib::HTTPResponse> PostSync(
      lib::HTTPRequest request, std::chrono::steady_clock::time_point deadline) override;

 private:
  const LoaderFactory loader_factory_;
  ::fuchsia::net::http::LoaderSyncPtr loader_;
};

}  // namespace utils
}  // namespace cobalt

#endif  // SRC_COBALT_BIN_UTILS_FUCHSIA_HTTP_CLIENT_H_
