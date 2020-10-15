// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/lib/dockyard/dockyard.h"
#include "src/developer/system_monitor/lib/dockyard/dockyard_service_impl.h"
#include "src/developer/system_monitor/lib/gt_log.h"

// The code below is not the real Dockyard. They are test/mock functions.
// See //src/developer/system_monitor/lib/dockyard/dockyard.cc for the actual
// code.
namespace dockyard {

// This is an arbitrary default port.
const char kDefaultServerAddress[] = "0.0.0.0:50051";

uint64_t MessageRequest::next_request_id_ = 0u;

Dockyard::Dockyard() = default;

Dockyard::~Dockyard() = default;

void Dockyard::AddSample(DockyardId dockyard_id, Sample sample) {}

void Dockyard::AddSamples(DockyardId dockyard_id,
                          const std::vector<Sample>& samples) {}

SampleTimeNs Dockyard::DeviceDeltaTimeNs() const { return 0; }

void Dockyard::SetDeviceTimeDeltaNs(SampleTimeNs /*delta_ns*/) {}

SampleTimeNs Dockyard::LatestSampleTimeNs() const { return 0; }

DockyardId Dockyard::GetDockyardId(const std::string& /*dockyard_path*/) {
  return 0;
}
bool Dockyard::GetDockyardPath(DockyardId /*dockyard_id*/,
                               std::string* /*dockyard_path*/) const {
  return false;
}
DockyardPathToIdMap Dockyard::MatchPaths(const std::string& /*starting*/,
                                         const std::string& /*ending*/) const {
  DockyardPathToIdMap result;
  return result;
}
bool Dockyard::HasDockyardPath(const std::string& dockyard_path,
                               DockyardId* dockyard_id) const {
  std::lock_guard<std::mutex> guard(mutex_);
  auto search = dockyard_path_to_id_.find(dockyard_path);
  if (search != dockyard_path_to_id_.end()) {
    *dockyard_id = search->second;
    return true;
  }
  return false;
}

void Dockyard::ResetHarvesterData() {
  std::lock_guard<std::mutex> guard(mutex_);
  device_time_delta_ns_ = 0;
  latest_sample_time_ns_ = 0;

  // Maybe send error responses.
  pending_get_requests_owned_.clear();
  pending_discard_requests_owned_.clear();

  sample_streams_.clear();
  sample_stream_low_high_.clear();

  dockyard_path_to_id_.clear();
  dockyard_id_to_path_.clear();

  DockyardId dockyard_id = GetDockyardId("<INVALID>");
  if (dockyard_id != INVALID_DOCKYARD_ID) {
    GT_LOG(ERROR) << "INVALID_DOCKYARD_ID string allocation failed. Exiting.";
    exit(1);
  }
}

void Dockyard::GetStreamSets(StreamSetsRequest&& /*request*/,
                             OnStreamSetsCallback /*callback*/) {}

void Dockyard::OnConnection(MessageType /*message_type*/,
                            uint32_t /*harvester_version*/) {}

bool Dockyard::StartCollectingFrom(ConnectionRequest&& /*request*/,
                                   OnConnectionCallback /*callback*/,
                                   std::string /*server_address*/) {
  return true;
}

void Dockyard::StopCollectingFromDevice() {}

OnPathsCallback Dockyard::SetDockyardPathsHandler(OnPathsCallback callback) {
  on_paths_handler_ = std::move(callback);
  return nullptr;
}

void Dockyard::ProcessRequests() {}

std::ostringstream Dockyard::DebugDump() const {
  std::ostringstream out;
  out << "Fake Dockyard::DebugDump" << std::endl;
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const StreamSetsRequest& /*request*/) {
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const StreamSetsResponse& /*response*/) {
  return out;
}

std::ostringstream DebugPrintQuery(const Dockyard& /*dockyard*/,
                                   const StreamSetsRequest& /*request*/,
                                   const StreamSetsResponse& /*response*/) {
  std::ostringstream out;
  return out;
}

// The following DockyardServiceImpl entries are for building with `--variant
// host_asan-ubsan`. They need to be defined to support RTTI used in ubsan.
grpc::Status DockyardServiceImpl::Init(grpc::ServerContext* context,
                  const dockyard_proto::InitRequest* request,
                  dockyard_proto::InitReply* reply) {
  return grpc::Status();
}

grpc::Status DockyardServiceImpl::SendInspectJson(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<dockyard_proto::EmptyMessage,
                             dockyard_proto::InspectJson>* stream) {
  return grpc::Status();
}

grpc::Status DockyardServiceImpl::SendSample(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<dockyard_proto::EmptyMessage,
                             dockyard_proto::RawSample>* stream) {
  return grpc::Status();
}

// Handler for the Harvester calling `SendSamples()`.
grpc::Status DockyardServiceImpl::SendSamples(
    grpc::ServerContext* context,
    grpc::ServerReaderWriter<dockyard_proto::EmptyMessage,
                             dockyard_proto::RawSamples>* stream) {
  return grpc::Status();
}

grpc::Status DockyardServiceImpl::GetDockyardIdsForPaths(
    grpc::ServerContext* context,
    const dockyard_proto::DockyardPaths* request,
    dockyard_proto::DockyardIds* reply) {
  return grpc::Status();
}

}  // namespace dockyard
