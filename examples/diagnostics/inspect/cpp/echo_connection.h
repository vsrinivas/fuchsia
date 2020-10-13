// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DIAGNOSTICS_INSPECT_CPP_ECHO_CONNECTION_H_
#define EXAMPLES_DIAGNOSTICS_INSPECT_CPP_ECHO_CONNECTION_H_

#include <lib/inspect/cpp/inspect.h>

#include <fidl/examples/echo/cpp/fidl.h>

namespace example {

struct EchoConnectionStats {
  inspect::ExponentialUintHistogram request_size_histogram;
  inspect::UintProperty total_requests;

  EchoConnectionStats(EchoConnectionStats&&) = default;
};

class EchoConnection : public fidl::examples::echo::Echo {
 public:
  explicit EchoConnection(inspect::Node node, std::weak_ptr<EchoConnectionStats> stats);
  virtual void EchoString(fidl::StringPtr value, EchoStringCallback callback);

 private:
  EchoConnection(const EchoConnection&) = delete;
  EchoConnection& operator=(const EchoConnection&) = delete;
  inspect::Node node_;
  inspect::UintProperty bytes_processed_;
  inspect::UintProperty requests_;
  std::weak_ptr<EchoConnectionStats> stats_;
};

}  // namespace example

#endif  // EXAMPLES_DIAGNOSTICS_INSPECT_CPP_ECHO_CONNECTION_H_
