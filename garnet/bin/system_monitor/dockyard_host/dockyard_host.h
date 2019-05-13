// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_DOCKYARD_HOST_DOCKYARD_HOST_H_
#define GARNET_BIN_SYSTEM_MONITOR_DOCKYARD_HOST_DOCKYARD_HOST_H_

#include <string>

#include "garnet/lib/system_monitor/dockyard/dockyard.h"

class SystemMonitorDockyardHostTest;

class DockyardHost {
 public:
  DockyardHost();

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
  dockyard::Dockyard dockyard_;
  std::string device_name_;
  dockyard::DockyardPathToIdMap path_to_id_;
  dockyard::DockyardIdToPathMap id_to_path_;
  dockyard::StreamSetsRequest request_;
  bool is_connected_;
  friend class ::SystemMonitorDockyardHostTest;
};

#endif  // GARNET_BIN_SYSTEM_MONITOR_DOCKYARD_HOST_DOCKYARD_HOST_H_
