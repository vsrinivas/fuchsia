// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef EXAMPLES_DIAGNOSTICS_INSPECT_CPP_ECHO_CONNECTION_H_
#define EXAMPLES_DIAGNOSTICS_INSPECT_CPP_ECHO_CONNECTION_H_

#include <fidl/examples/routing/echo/cpp/fidl.h>
#include <lib/inspect/cpp/inspect.h>

namespace example {

struct EchoConnectionStats {
  inspect::UintProperty bytes_processed;
  inspect::UintProperty total_requests;

  EchoConnectionStats(EchoConnectionStats&&) = default;
};

class EchoConnection : public fidl::examples::routing::echo::Echo {
 public:
  explicit EchoConnection(std::weak_ptr<EchoConnectionStats> stats);
  void EchoString(::fidl::StringPtr value, EchoStringCallback callback) override;

 private:
  EchoConnection(const EchoConnection&) = delete;
  EchoConnection& operator=(const EchoConnection&) = delete;
  std::weak_ptr<EchoConnectionStats> stats_;
};

}  // namespace example

#endif  // EXAMPLES_DIAGNOSTICS_INSPECT_CPP_ECHO_CONNECTION_H_
