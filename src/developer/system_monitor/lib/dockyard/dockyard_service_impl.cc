// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/lib/dockyard/dockyard_service_impl.h"

#include "src/developer/system_monitor/lib/dockyard/dockyard.h"
#include "src/developer/system_monitor/lib/gt_log.h"

namespace dockyard {

grpc::Status DockyardServiceImpl::Init(
    grpc::ServerContext* context, const dockyard_proto::InitRequest* request,
    dockyard_proto::InitReply* reply) {
  auto now = std::chrono::system_clock::now();
  auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                         now.time_since_epoch())
                         .count();
  dockyard_->SetDeviceTimeDeltaNs(nanoseconds - request->device_time_ns());
  if (request->version() != DOCKYARD_VERSION) {
    dockyard_->OnConnection(MessageType::kVersionMismatch, request->version());
    return grpc::Status::CANCELLED;
  }
  reply->set_version(DOCKYARD_VERSION);
  dockyard_->OnConnection(MessageType::kResponseOk, request->version());
  return grpc::Status::OK;
}

grpc::Status DockyardServiceImpl::SendInspectJson(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<dockyard_proto::EmptyMessage,
                             dockyard_proto::InspectJson>* stream) {
  dockyard_proto::InspectJson inspect;
  while (stream->Read(&inspect)) {
    GT_LOG(INFO) << "Received inspect at " << inspect.time() << ", key "
                 << inspect.dockyard_id() << ": " << inspect.json();
    // TODO(fxbug.dev/43): interpret the data.
  }
  return grpc::Status::OK;
}

grpc::Status DockyardServiceImpl::SendSample(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<dockyard_proto::EmptyMessage,
                             dockyard_proto::RawSample>* stream) {
  dockyard_proto::RawSample sample;
  while (stream->Read(&sample)) {
    GT_LOG(INFO) << "Received sample at " << sample.time() << ", key "
                 << sample.sample().key() << ": " << sample.sample().value();

    dockyard_->AddSample(sample.sample().key(),
                         Sample(sample.time(), sample.sample().value()));
  }
  return grpc::Status::OK;
}

grpc::Status DockyardServiceImpl::SendSamples(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<dockyard_proto::EmptyMessage,
                             dockyard_proto::RawSamples>* stream) {
  dockyard_proto::RawSamples samples;
  while (stream->Read(&samples)) {
    int limit = samples.sample_size();
    for (int i = 0; i < limit; ++i) {
      auto sample = samples.sample(i);
      dockyard_->AddSample(sample.key(),
                           Sample(samples.time(), sample.value()));
    }
  }
  return grpc::Status::OK;
}

grpc::Status DockyardServiceImpl::GetDockyardIdsForPaths(
    grpc::ServerContext* context, const dockyard_proto::DockyardPaths* request,
    dockyard_proto::DockyardIds* reply) {
  for (int i = 0; i < request->path_size(); ++i) {
    DockyardId id = dockyard_->GetDockyardId(request->path(i));
    reply->add_id(id);
    GT_LOG(DEBUG) << "Allocated DockyardIds "
                  << ": " << request->path(i) << ", id " << id;
  }
  return grpc::Status::OK;
}

}  // namespace dockyard
