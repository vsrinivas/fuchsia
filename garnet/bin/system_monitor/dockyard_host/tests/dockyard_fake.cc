// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/system_monitor/dockyard/dockyard.h"

namespace dockyard {

// The functions below are not the real Dockyard. They are test/mock functions.
// See //garnet/lib/system_monitor/dockyard/dockyard.cc for the actual code.

Dockyard::Dockyard() {}

Dockyard::~Dockyard() {}

void Dockyard::AddSample(DockyardId dockyard_id, Sample sample) {}

void Dockyard::AddSamples(DockyardId dockyard_id, std::vector<Sample> samples) {
}

SampleTimeNs Dockyard::DeviceDeltaTimeNs() const { return 0; }

void Dockyard::SetDeviceTimeDeltaNs(SampleTimeNs delta_ns) {}

SampleTimeNs Dockyard::LatestSampleTimeNs() const { return 0; }

DockyardId Dockyard::GetDockyardId(const std::string& dockyard_path) {
  return 0;
}

uint64_t Dockyard::GetStreamSets(StreamSetsRequest* request) { return 0; }

void Dockyard::OnConnection() {}

void Dockyard::StartCollectingFrom(const std::string& device) {}

void Dockyard::StopCollectingFrom(const std::string& device) {}

OnConnectionCallback Dockyard::SetConnectionHandler(
    OnConnectionCallback callback) {
  on_connection_handler_ = callback;
  return nullptr;
}

OnPathsCallback Dockyard::SetDockyardPathsHandler(OnPathsCallback callback) {
  on_paths_handler_ = callback;
  return nullptr;
}

OnStreamSetsCallback Dockyard::SetStreamSetsHandler(
    OnStreamSetsCallback callback) {
  on_stream_sets_handler_ = callback;
  return nullptr;
}

void Dockyard::ProcessRequests() {}

std::ostream& operator<<(std::ostream& out, const StreamSetsRequest& request) {
  return out;
}

std::ostream& operator<<(std::ostream& out,
                         const StreamSetsResponse& response) {
  return out;
}

}  // namespace dockyard
