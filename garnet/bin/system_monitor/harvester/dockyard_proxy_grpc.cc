// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <memory>
#include <string>

#include <zircon/status.h>

#include "dockyard_proxy.h"
#include "dockyard_proxy_grpc.h"
#include "garnet/lib/system_monitor/protos/dockyard.grpc.pb.h"
#include "src/lib/fxl/logging.h"

namespace harvester {

namespace {

constexpr DockyardProxyStatus ToDockyardProxyStatus(
    const grpc::Status& status) {
  return status.ok() ? DockyardProxyStatus::OK : DockyardProxyStatus::ERROR;
}

}  // namespace

DockyardProxyStatus DockyardProxyGrpc::Init() {
  dockyard_proto::InitRequest request;
  request.set_device_name("TODO SET DEVICE NAME");
  request.set_version(dockyard::DOCKYARD_VERSION);
  auto now = std::chrono::system_clock::now();
  uint64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             now.time_since_epoch())
                             .count();
  request.set_device_time_ns(nanoseconds);
  dockyard_proto::InitReply reply;

  grpc::ClientContext context;
  grpc::Status status = stub_->Init(&context, request, &reply);
  if (status.ok()) {
    return DockyardProxyStatus::OK;
  }
  FXL_LOG(ERROR) << status.error_code() << ": " << status.error_message();
  FXL_LOG(ERROR) << "Unable to send to dockyard.";
  return DockyardProxyStatus::ERROR;
}

DockyardProxyStatus DockyardProxyGrpc::SendInspectJson(
    const std::string& dockyard_path, const std::string& json) {
  auto now = std::chrono::system_clock::now();
  uint64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             now.time_since_epoch())
                             .count();
  dockyard::DockyardId dockyard_id;
  grpc::Status status = GetDockyardIdForPath(&dockyard_id, dockyard_path);
  if (status.ok()) {
    return ToDockyardProxyStatus(
        SendInspectJsonById(nanoseconds, dockyard_id, json));
  }
  return ToDockyardProxyStatus(status);
}

DockyardProxyStatus DockyardProxyGrpc::SendSample(
    const std::string& dockyard_path, uint64_t value) {
  // TODO(dschuyler): system_clock might be at usec resolution. Consider using
  // high_resolution_clock.
  auto now = std::chrono::system_clock::now();
  uint64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             now.time_since_epoch())
                             .count();
  dockyard::DockyardId dockyard_id;
  grpc::Status status = GetDockyardIdForPath(&dockyard_id, dockyard_path);
  if (status.ok()) {
    return ToDockyardProxyStatus(
        SendSampleById(nanoseconds, dockyard_id, value));
  }
  return ToDockyardProxyStatus(status);
}

DockyardProxyStatus DockyardProxyGrpc::SendSampleList(const SampleList list) {
  // TODO(dschuyler): system_clock might be at usec resolution. Consider using
  // high_resolution_clock.
  auto now = std::chrono::system_clock::now();
  uint64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             now.time_since_epoch())
                             .count();
  SampleListById by_id(list.size());
  auto path_iter = list.begin();
  auto id_iter = by_id.begin();
  for (; path_iter != list.end(); ++path_iter, ++id_iter) {
    dockyard::DockyardId dockyard_id;
    grpc::Status status = GetDockyardIdForPath(&dockyard_id, path_iter->first);
    if (!status.ok()) {
      return ToDockyardProxyStatus(status);
    }
    id_iter->first = dockyard_id;
    id_iter->second = path_iter->second;
  }
  return ToDockyardProxyStatus(SendSampleListById(nanoseconds, by_id));
}

grpc::Status DockyardProxyGrpc::SendInspectJsonById(
    uint64_t time, dockyard::DockyardId dockyard_id, const std::string& json) {
  // Data we are sending to the server.
  dockyard_proto::InspectJson inspect;
  inspect.set_time(time);
  inspect.set_dockyard_id(dockyard_id);
  inspect.set_json(json);

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriter<dockyard_proto::InspectJson,
                                           dockyard_proto::EmptyMessage>>
      stream(stub_->SendInspectJson(&context));

  stream->Write(inspect);
  stream->WritesDone();
  return stream->Finish();
}

grpc::Status DockyardProxyGrpc::SendSampleById(uint64_t time,
                                               dockyard::DockyardId dockyard_id,
                                               uint64_t value) {
  // Data we are sending to the server.
  dockyard_proto::RawSample sample;
  sample.set_time(time);
  sample.mutable_sample()->set_key(dockyard_id);
  sample.mutable_sample()->set_value(value);

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriter<dockyard_proto::RawSample,
                                           dockyard_proto::EmptyMessage>>
      stream(stub_->SendSample(&context));

  stream->Write(sample);
  stream->WritesDone();
  return stream->Finish();
}

grpc::Status DockyardProxyGrpc::SendSampleListById(uint64_t time,
                                                   const SampleListById list) {
  // Data we are sending to the server.
  dockyard_proto::RawSamples samples;
  samples.set_time(time);
  for (auto iter = list.begin(); iter != list.end(); ++iter) {
    auto sample = samples.add_sample();
    sample->set_key(iter->first);
    sample->set_value(iter->second);
  }

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriter<dockyard_proto::RawSamples,
                                           dockyard_proto::EmptyMessage>>
      stream(stub_->SendSamples(&context));

  stream->Write(samples);
  stream->WritesDone();
  return stream->Finish();
}

grpc::Status DockyardProxyGrpc::GetDockyardIdForPath(
    dockyard::DockyardId* dockyard_id, const std::string& dockyard_path) {
  auto iter = dockyard_path_to_id_.find(dockyard_path);
  if (iter != dockyard_path_to_id_.end()) {
    *dockyard_id = iter->second;
    return grpc::Status::OK;
  }

  dockyard_proto::DockyardPathMessage path;
  path.set_path(dockyard_path);

  // Container for the data we expect from the server.
  dockyard_proto::DockyardIdMessage reply;

  grpc::ClientContext context;
  grpc::Status status = stub_->GetDockyardIdForPath(&context, path, &reply);
  if (status.ok()) {
    *dockyard_id = reply.id();
    // Memoize it.
    dockyard_path_to_id_.emplace(dockyard_path, *dockyard_id);
  }
  return status;
}

}  // namespace harvester
