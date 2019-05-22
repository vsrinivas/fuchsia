// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_INSPECTION_ECHO_CONNECTION_H_
#define GARNET_EXAMPLES_INSPECTION_ECHO_CONNECTION_H_

#include <fidl/examples/echo/cpp/fidl.h>
#include <lib/inspect/inspect.h>

namespace example {

struct EchoConnectionStats {
  inspect::ExponentialUIntHistogramMetric request_size_histogram;
  inspect::UIntMetric total_requests;

  EchoConnectionStats(EchoConnectionStats&&) = default;
};

class EchoConnection : public fidl::examples::echo::Echo {
 public:
  explicit EchoConnection(inspect::Node node,
                          std::weak_ptr<EchoConnectionStats> stats);
  virtual void EchoString(fidl::StringPtr value, EchoStringCallback callback);

 private:
  EchoConnection(const EchoConnection&) = delete;
  EchoConnection& operator=(const EchoConnection&) = delete;
  inspect::Node node_;
  inspect::UIntMetric bytes_processed_;
  inspect::UIntMetric requests_;
  std::weak_ptr<EchoConnectionStats> stats_;
};

}  // namespace example

#endif  // GARNET_EXAMPLES_INSPECTION_ECHO_CONNECTION_H_
