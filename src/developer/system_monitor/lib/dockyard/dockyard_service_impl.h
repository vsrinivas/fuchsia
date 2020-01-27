// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_DOCKYARD_SERVICE_IMPL_H_
#define GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_DOCKYARD_SERVICE_IMPL_H_

#include "src/developer/system_monitor/lib/proto/dockyard.grpc.pb.h"

#include <grpc++/grpc++.h>

namespace dockyard {
class Dockyard;

// Logic and data behind the server's behavior.
class DockyardServiceImpl final : public dockyard_proto::Dockyard::Service {
 public:
  void SetDockyard(Dockyard* dockyard) { dockyard_ = dockyard; }

 private:
  Dockyard* dockyard_;

  grpc::Status Init(grpc::ServerContext* context,
                    const dockyard_proto::InitRequest* request,
                    dockyard_proto::InitReply* reply) override;

  grpc::Status SendInspectJson(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<dockyard_proto::EmptyMessage,
                               dockyard_proto::InspectJson>* stream) override;

  // This is the handler for the client sending a `SendSample` message. A better
  // name would be `ReceiveSample` but then it wouldn't match the message
  // name.
  grpc::Status SendSample(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<dockyard_proto::EmptyMessage,
                               dockyard_proto::RawSample>* stream) override;

  // Handler for the Harvester calling `SendSamples()`.
  grpc::Status SendSamples(
      grpc::ServerContext* context,
      grpc::ServerReaderWriter<dockyard_proto::EmptyMessage,
                               dockyard_proto::RawSamples>* stream) override;

  grpc::Status GetDockyardIdsForPaths(
      grpc::ServerContext* context,
      const dockyard_proto::DockyardPaths* request,
      dockyard_proto::DockyardIds* reply) override;
};
}  // namespace dockyard

#endif  // GARNET_LIB_SYSTEM_MONITOR_DOCKYARD_DOCKYARD_SERVICE_IMPL_H_
