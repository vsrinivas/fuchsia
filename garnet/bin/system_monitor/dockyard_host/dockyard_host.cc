// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/system_monitor/dockyard_host/dockyard_host.h"

#include <time.h>

#include <chrono>
#include <thread>

#include "src/lib/fxl/logging.h"

DockyardHost::DockyardHost() : is_connected_(false) {
  dockyard_.SetConnectionHandler(
      std::bind(&DockyardHost::OnConnection, this, std::placeholders::_1));
  dockyard_.SetDockyardPathsHandler(std::bind(&DockyardHost::OnPaths, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
  dockyard_.SetStreamSetsHandler(
      std::bind(&DockyardHost::OnStreamSets, this, std::placeholders::_1));
}

void DockyardHost::StartCollectingFrom(const std::string& device_name) {
  dockyard_.StartCollectingFrom(device_name);
  device_name_ = device_name;
}

void DockyardHost::OnConnection(const std::string& device_name) {
  FXL_LOG(INFO) << "OnConnection from \"" << device_name << "\".";
  is_connected_ = true;

  // Check that the device is sending the total memory.
  request_.start_time_ns = dockyard_.LatestSampleTimeNs();
  request_.end_time_ns = dockyard_.LatestSampleTimeNs();
  request_.sample_count = 1;
  request_.render_style = dockyard::StreamSetsRequest::HIGHEST_PER_COLUMN;
  request_.dockyard_ids.push_back(
      dockyard_.GetDockyardId("memory:device_total_bytes"));
  dockyard_.GetStreamSets(&request_);
}

void DockyardHost::OnPaths(const std::vector<dockyard::PathInfo>& add,
                           const std::vector<dockyard::DockyardId>& remove) {
  FXL_LOG(INFO) << "OnPaths";
  for (const auto& path_info : add) {
    FXL_LOG(INFO) << "  add " << path_info.id << ": " << path_info.path;
    path_to_id_.emplace(path_info.path, path_info.id);
    id_to_path_.emplace(path_info.id, path_info.path);
  }
  for (const auto& dockyard_id : remove) {
    FXL_LOG(INFO) << "  remove " << dockyard_id;
    auto search = id_to_path_.find(dockyard_id);
    if (search != id_to_path_.end()) {
      path_to_id_.erase(search->second);
      id_to_path_.erase(search);
    }
  }
}

void DockyardHost::OnStreamSets(const dockyard::StreamSetsResponse& response) {
  if (response.request_id != request_.request_id) {
    FXL_LOG(INFO) << "OnStreamSets request_id " << response.request_id
                  << " != " << request_.request_id;
  }
  FXL_LOG(INFO) << "OnStreamSets " << response;

  // For now this is hard-coded to get the memory:device_total_bytes.
  FXL_LOG(INFO) << "memory:device_total_bytes " << response.lowest_value;
}
