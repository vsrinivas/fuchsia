// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/system_monitor/bin/dockyard_host/dockyard_host.h"

#include <time.h>

#include <chrono>
#include <fstream>
#include <future>
#include <thread>

#include "src/developer/system_monitor/lib/gt_log.h"

namespace {

// Generic function to extract the values (without the keys) from a map.
template <typename K, typename V>
std::vector<V> GetMapValues(std::map<K, V> input) {
  std::vector<V> result;
  result.reserve(input.size());
  for (const auto& entry : input) {
    result.push_back(entry.second);
  }
  return result;
}

// Test a multi-step query. First the set of named kernel objects is collected,
// then the IDs are determined, the IDs are translated into strings, and final
// results are printed to the log.
void TestFetchKoidNames(DockyardHost* dockyard_host,
                        const dockyard::Dockyard& dockyard) {
  dockyard::DockyardPathToIdMap paths = dockyard.MatchPaths("koid:", ":name");
  auto names = dockyard_host->GetSampleStringsForIds(GetMapValues(paths));
  if (!names) {
    return;
  }
  if (names->size() != paths.size()) {
    GT_LOG(WARNING) << "names and paths size mismatch."
                    << " names " << names->size() << ", paths " << paths.size();
    return;
  }
  auto name = names->begin();
  auto path = paths.begin();
  for (; name < names->end(); ++name, ++path) {
    GT_LOG(INFO) << path->second << "=" << path->first << ": '" << *name << "'";
  }
}

}  // namespace

DockyardHost::DockyardHost() : is_connected_(false) {
  // Set up callback handlers.
  dockyard_.SetDockyardPathsHandler(std::bind(&DockyardHost::OnPaths, this,
                                              std::placeholders::_1,
                                              std::placeholders::_2));
}

void DockyardHost::StartCollectingFrom(const std::string& device_name) {
  dockyard::ConnectionRequest request;
  request.SetDeviceName(device_name);
  bool started = dockyard_.StartCollectingFrom(
      std::move(request),
      [this, device_name](const dockyard::ConnectionRequest& /*request*/,
                          const dockyard::ConnectionResponse& response) {
        if (!response.Ok()) {
          GT_LOG(FATAL) << "StartCollectingFrom failed";
          return;
        }
        OnConnection(device_name);
      });
  if (!started) {
    GT_LOG(FATAL) << "Call StopCollectingFromDevice before calling"
                     " StartCollectingFrom again";
  }
}

std::optional<std::future<std::unique_ptr<AsyncQuery>>>
DockyardHost::GetSamples(const std::vector<dockyard::DockyardId>& path_ids) {
  // Create a query instance.
  auto emplacement = std::make_unique<AsyncQuery>();
  auto iter = request_id_to_async_query_.emplace(
      emplacement->request.RequestId(), std::move(emplacement));
  if (!iter.second) {
    GT_LOG(ERROR) << "Failed to emplace query (might be out of"
                     " memory).";
    return {};
  }
  auto query = iter.first->second.get();
  // Fill in the request.
  query->request.start_time_ns = 0;
  query->request.end_time_ns = dockyard_.LatestSampleTimeNs();
  query->request.sample_count = 1;
  query->request.render_style = dockyard::StreamSetsRequest::RECENT;
  query->request.dockyard_ids.insert(query->request.dockyard_ids.begin(),
                                     path_ids.begin(), path_ids.end());
  // Issue the request for the data.
  dockyard_.GetStreamSets(std::move(iter.first->second->request),
                          [this](const dockyard::StreamSetsRequest& /*request*/,
                                 const dockyard::StreamSetsResponse& response) {
                            OnStreamSets(response);
                          });

  return std::optional<std::future<std::unique_ptr<AsyncQuery>>>{
      iter.first->second->promise.get_future()};
}

std::optional<dockyard::SampleValue> DockyardHost::GetSampleValue(
    const std::string& path) {
  std::vector<dockyard::DockyardId> dockyard_ids;
  dockyard_ids.emplace_back(dockyard_.GetDockyardId(path));
  auto future = GetSamples(dockyard_ids);
  if (!future) {
    return {};
  }
  return future->get()->response.highest_value;
}

std::optional<std::string> DockyardHost::GetSampleString(
    const std::string& path) {
  std::vector<dockyard::DockyardId> dockyard_ids;
  dockyard_ids.emplace_back(dockyard_.GetDockyardId(path));
  auto future = GetSamples(dockyard_ids);
  if (!future) {
    return {};
  }
  dockyard::DockyardId dockyard_id = future->get()->response.highest_value;
  std::string string_as_path;
  if (dockyard_.GetDockyardPath(dockyard_id, &string_as_path)) {
    return string_as_path;
  }
  return {};
}

std::optional<std::vector<const std::string>>
DockyardHost::GetSampleStringsForIds(
    const std::vector<dockyard::DockyardId>& path_ids) {
  auto future = GetSamples(path_ids);
  if (!future) {
    return {};
  }
  std::unique_ptr<AsyncQuery> query = future->get();
  GT_LOG(DEBUG) << "GetSampleStringsForIds query "
                << dockyard::DebugPrintQuery(dockyard_, query->request,
                                             query->response)
                       .str();
  std::vector<const std::string> result;
  for (const auto& sample_values : query->response.data_sets) {
    std::string string_as_path;
    if (dockyard_.GetDockyardPath(sample_values[0], &string_as_path)) {
      result.emplace_back(string_as_path);
    } else {
      result.emplace_back("<not found>");
    }
  }
  return result;
}

void DockyardHost::OnConnection(const std::string& device_name) {
  GT_LOG(DEBUG) << "Connection from " << device_name;
  is_connected_ = true;
  device_name_ = device_name;

  // This might not be the right choice for all clients. For this application
  // starting fresh with the new connection is a reasonable approach.
  dockyard_.ResetHarvesterData();

  // Run some tests.
  run_tests_ = std::async([this]() {
    // Give time for the dockyard to populate some samples.
    std::this_thread::sleep_for(std::chrono::seconds(4));

    // How much RAM does the Fuchsia device have.
    GT_LOG(INFO) << "memory:device_total_bytes "
                 << *GetSampleValue("memory:device_total_bytes");

    // How much RAM does the Fuchsia device have.
    GT_LOG(INFO) << "cpu:0:busy_time " << *GetSampleValue("cpu:0:busy_time");

    // Dump the dockyard state to a file.
    if (dump_state_) {
      std::ofstream out_file;
      out_file.open("dockyard_dump");
      out_file << dockyard_.DebugDump().str();
      out_file.close();
    }

    // Print a list of all the named kernel objects.
    TestFetchKoidNames(this, dockyard_);
  });
}

void DockyardHost::OnPaths(const std::vector<dockyard::PathInfo>& add,
                           const std::vector<dockyard::DockyardId>& remove) {
  GT_LOG(DEBUG) << "OnPaths";
  for (const auto& path_info : add) {
    GT_LOG(DEBUG) << "  add " << path_info.id << ": " << path_info.path;
    path_to_id_.emplace(path_info.path, path_info.id);
    id_to_path_.emplace(path_info.id, path_info.path);
  }
  for (const auto& dockyard_id : remove) {
    GT_LOG(DEBUG) << "  remove " << dockyard_id;
    auto search = id_to_path_.find(dockyard_id);
    if (search != id_to_path_.end()) {
      path_to_id_.erase(search->second);
      id_to_path_.erase(search);
    }
  }
}

void DockyardHost::OnStreamSets(const dockyard::StreamSetsResponse& response) {
  auto search = request_id_to_async_query_.find(response.RequestId());
  if (search != request_id_to_async_query_.end()) {
    search->second->response = response;
    search->second->promise.set_value(std::move(search->second));
    request_id_to_async_query_.erase(search);
  } else {
    GT_LOG(INFO) << "Did not find RequestId " << response.RequestId();
  }
}
