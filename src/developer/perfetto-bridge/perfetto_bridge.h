// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_PERFETTO_BRIDGE_PERFETTO_BRIDGE_H_
#define SRC_DEVELOPER_PERFETTO_BRIDGE_PERFETTO_BRIDGE_H_

#include <fuchsia/tracing/perfetto/cpp/fidl.h>

class PerfettoBridge : public fuchsia::tracing::perfetto::ProducerConnector,
                       public fuchsia::tracing::perfetto::ConsumerConnector {
 public:
  PerfettoBridge();
  ~PerfettoBridge();

  PerfettoBridge(const PerfettoBridge&) = delete;
  void operator=(const PerfettoBridge&) = delete;

  // fuchsia::tracing::perfetto::ProducerConnector implementation.
  void ConnectProducer(::zx::socket producer_socket, ::zx::vmo trace_buffer,
                       ConnectProducerCallback callback) final;

  // fuchsia::tracing::perfetto::ConsumerConnector
  void ConnectConsumer(::zx::socket consumer_socket, ConnectConsumerCallback callback) final;
};

#endif  // SRC_DEVELOPER_PERFETTO_BRIDGE_PERFETTO_BRIDGE_H_
