// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/perfetto-bridge/perfetto_bridge.h"

#include <lib/syslog/cpp/macros.h>

PerfettoBridge::PerfettoBridge() = default;

PerfettoBridge::~PerfettoBridge() = default;

// fuchsia::tracing::perfetto::ProducerConnector implementation.
void PerfettoBridge::ConnectProducer(::zx::socket producer_socket,
                                     ::fuchsia::tracing::perfetto::TraceBuffer buffer,
                                     ConnectProducerCallback callback) {
  FX_NOTIMPLEMENTED();
}

// fuchsia::tracing::perfetto::ConsumerConnector
void PerfettoBridge::ConnectConsumer(::zx::socket consumer_socket,
                                     ConnectConsumerCallback callback) {
  FX_NOTIMPLEMENTED();
}
