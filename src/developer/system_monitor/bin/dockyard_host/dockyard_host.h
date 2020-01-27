// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_DOCKYARD_HOST_DOCKYARD_HOST_H_
#define GARNET_BIN_SYSTEM_MONITOR_DOCKYARD_HOST_DOCKYARD_HOST_H_

#include <future>
#include <map>
#include <optional>
#include <string>

#include "src/developer/system_monitor/lib/dockyard/dockyard.h"

class SystemMonitorDockyardHostTest;

// Helper struct to track a request, response, and a promise to resolve when
// response arrives.
struct AsyncQuery {
  std::promise<std::unique_ptr<AsyncQuery>> promise;
  dockyard::StreamSetsRequest request;
  dockyard::StreamSetsResponse response;
};

// Associate a request context ID with an AsyncQuery.
using RequestIdToAsyncQuery = std::map<uint64_t, std::unique_ptr<AsyncQuery>>;

// The DockyardHost is a demonstration application for the Harvester component
// and the Dockyard library. This makes it possible to test queries independent
// of the GUI, for example.
class DockyardHost {
 public:
  DockyardHost();

  // Request the current value of a sample.
  std::optional<std::future<std::unique_ptr<AsyncQuery>>> GetSamples(
      const std::vector<dockyard::DockyardId>& path_ids);

  // Get an integer value for a given Dockyard path.
  std::optional<dockyard::SampleValue> GetSampleValue(const std::string& path);

  // Get a string result for a given Dockyard path.
  std::optional<std::string> GetSampleString(const std::string& path);

  // Get a list of string results for a given list of Dockyard IDs.
  std::optional<std::vector<const std::string>> GetSampleStringsForIds(
      const std::vector<dockyard::DockyardId>& path_ids);

  // Access the host's Dockyard instance.
  dockyard::Dockyard& Dockyard() { return dockyard_; }

  // As Dockyard::StartCollectingFrom.
  void StartCollectingFrom(const std::string& device_name);

  // Called by the dockyard when a connection to a Fuchsia device is made.
  void OnConnection(const std::string& device_name);

  // Called by the dockyard when paths/strings are created or removed.
  void OnPaths(const std::vector<dockyard::PathInfo>& add,
               const std::vector<dockyard::DockyardId>& remove);

  // Called by the dockyard stream sets arrive.
  void OnStreamSets(const dockyard::StreamSetsResponse& response);

 private:
  // The backend this code is hosting.
  dockyard::Dockyard dockyard_;

  // The four word (three-dotted) name of the Fuchsia device.
  std::string device_name_;

  // Mapping paths and Dockyard IDs.
  dockyard::DockyardPathToIdMap path_to_id_;
  dockyard::DockyardIdToPathMap id_to_path_;

  // The pending (and resolved) requests.
  RequestIdToAsyncQuery request_id_to_async_query_;

  // Whether to dump the dockyard state to a file.
  bool dump_state_ = false;

  // Tests are run asynchronously.
  std::future<void> run_tests_;

  // Whether a Fuchsia device has connected to the |dockyard_|.
  bool is_connected_;

  // Get a Dockyard path to this process with the supplied |suffix|.
  std::string KoidPath(const std::string& suffix);

  friend class ::SystemMonitorDockyardHostTest;
};

#endif  // GARNET_BIN_SYSTEM_MONITOR_DOCKYARD_HOST_DOCKYARD_HOST_H_
