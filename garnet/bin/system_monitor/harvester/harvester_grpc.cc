// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <chrono>
#include <memory>
#include <string>

#include <fuchsia/memory/cpp/fidl.h>
#include <zircon/status.h>

#include "garnet/lib/system_monitor/protos/dockyard.grpc.pb.h"
#include "harvester.h"
#include "harvester_grpc.h"
#include "lib/fxl/logging.h"

namespace harvester {

namespace {

constexpr HarvesterStatus ToHarvesterStatus(const grpc::Status& status) {
  return status.ok() ? HarvesterStatus::OK : HarvesterStatus::ERROR;
}

}  // namespace

HarvesterStatus HarvesterGrpc::Init() {
  dockyard_proto::InitRequest request;
  request.set_name("TODO SET DEVICE NAME");
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
    return HarvesterStatus::OK;
  }
  FXL_LOG(ERROR) << status.error_code() << ": " << status.error_message();
  FXL_LOG(ERROR) << "Unable to send to dockyard.";
  return HarvesterStatus::ERROR;
}

HarvesterStatus HarvesterGrpc::SendInspectJson(const std::string& stream_name,
                                               const std::string& json) {
  auto now = std::chrono::system_clock::now();
  uint64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             now.time_since_epoch())
                             .count();
  dockyard::SampleStreamId stream_id;
  grpc::Status status = GetStreamIdForName(&stream_id, stream_name);
  if (status.ok()) {
    return ToHarvesterStatus(SendInspectJsonById(nanoseconds, stream_id, json));
  }
  return ToHarvesterStatus(status);
}

HarvesterStatus HarvesterGrpc::SendSample(const std::string& stream_name,
                                          uint64_t value) {
  // TODO(dschuyler): system_clock might be at usec resolution. Consider using
  // high_resolution_clock.
  auto now = std::chrono::system_clock::now();
  uint64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             now.time_since_epoch())
                             .count();
  dockyard::SampleStreamId stream_id;
  grpc::Status status = GetStreamIdForName(&stream_id, stream_name);
  if (status.ok()) {
    return ToHarvesterStatus(SendSampleById(nanoseconds, stream_id, value));
  }
  return ToHarvesterStatus(status);
}

HarvesterStatus HarvesterGrpc::SendSampleList(const SampleList list) {
  // TODO(dschuyler): system_clock might be at usec resolution. Consider using
  // high_resolution_clock.
  auto now = std::chrono::system_clock::now();
  uint64_t nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             now.time_since_epoch())
                             .count();
  SampleListById by_id(list.size());
  auto name_iter = list.begin();
  auto id_iter = by_id.begin();
  for (; name_iter != list.end(); ++name_iter, ++id_iter) {
    dockyard::SampleStreamId stream_id;
    grpc::Status status = GetStreamIdForName(&stream_id, name_iter->first);
    if (!status.ok()) {
      return ToHarvesterStatus(status);
    }
    id_iter->first = stream_id;
    id_iter->second = name_iter->second;
  }
  return ToHarvesterStatus(SendSampleListById(nanoseconds, by_id));
}

grpc::Status HarvesterGrpc::SendInspectJsonById(
    uint64_t time, dockyard::SampleStreamId stream_id,
    const std::string& json) {
  // Data we are sending to the server.
  dockyard_proto::InspectJson inspect;
  inspect.set_time(time);
  inspect.set_id(stream_id);
  inspect.set_json(json);

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriter<dockyard_proto::InspectJson,
                                           dockyard_proto::EmptyMessage>>
      stream(stub_->SendInspectJson(&context));

  stream->Write(inspect);
  stream->WritesDone();
  return stream->Finish();
}

grpc::Status HarvesterGrpc::SendSampleById(uint64_t time,
                                           dockyard::SampleStreamId stream_id,
                                           uint64_t value) {
  // Data we are sending to the server.
  dockyard_proto::RawSample sample;
  sample.set_time(time);
  sample.mutable_sample()->set_key(stream_id);
  sample.mutable_sample()->set_value(value);

  grpc::ClientContext context;
  std::shared_ptr<grpc::ClientReaderWriter<dockyard_proto::RawSample,
                                           dockyard_proto::EmptyMessage>>
      stream(stub_->SendSample(&context));

  stream->Write(sample);
  stream->WritesDone();
  return stream->Finish();
}

grpc::Status HarvesterGrpc::SendSampleListById(uint64_t time,
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

grpc::Status HarvesterGrpc::GetStreamIdForName(
    dockyard::SampleStreamId* stream_id, const std::string& stream_name) {
  auto iter = stream_ids_.find(stream_name);
  if (iter != stream_ids_.end()) {
    *stream_id = iter->second;
    return grpc::Status::OK;
  }

  dockyard_proto::StreamNameMessage name;
  name.set_name(stream_name);

  // Container for the data we expect from the server.
  dockyard_proto::StreamIdMessage reply;

  grpc::ClientContext context;
  grpc::Status status = stub_->GetStreamIdForName(&context, name, &reply);
  if (status.ok()) {
    *stream_id = reply.id();
    // Memoize it.
    stream_ids_.emplace(stream_name, *stream_id);
  }
  return status;
}

}  // namespace harvester
