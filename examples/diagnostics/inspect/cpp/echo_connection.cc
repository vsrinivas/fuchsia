// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "echo_connection.h"

#include <lib/sys/cpp/component_context.h>

namespace example {

EchoConnection::EchoConnection(inspect::Node node, std::weak_ptr<EchoConnectionStats> stats)
    : node_(std::move(node)), stats_(std::move(stats)) {
  bytes_processed_ = node_.CreateUint("bytes_processed", 0);
  requests_ = node_.CreateUint("requests", 0);
}

void EchoConnection::EchoString(fidl::StringPtr value, EchoStringCallback callback) {
  requests_.Add(1);
  bytes_processed_.Add(value->size());
  auto stats = stats_.lock();
  if (stats) {
    stats->request_size_histogram.Insert(value->size(), 1);
    stats->total_requests.Add(1);
  }
  callback(std::move(value));
}

}  // namespace example
