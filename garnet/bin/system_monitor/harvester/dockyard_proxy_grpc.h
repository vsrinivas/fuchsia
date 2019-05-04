// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_GRPC_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_GRPC_H_

#include <grpc++/grpc++.h>

#include "dockyard_proxy.h"
#include "garnet/lib/system_monitor/dockyard/dockyard.h"
#include "garnet/lib/system_monitor/protos/dockyard.grpc.pb.h"

namespace harvester {

class DockyardProxyGrpc : public DockyardProxy {
 public:
  DockyardProxyGrpc(std::shared_ptr<grpc::Channel> channel)
      : stub_(dockyard_proto::Dockyard::NewStub(channel)) {}

  // |DockyardProxy|.
  virtual DockyardProxyStatus Init() override;

  // |DockyardProxy|.
  virtual DockyardProxyStatus SendInspectJson(const std::string& dockyard_path,
                                              const std::string& json) override;

  // |DockyardProxy|.
  virtual DockyardProxyStatus SendSample(const std::string& dockyard_path,
                                         uint64_t value) override;

  // |DockyardProxy|.
  virtual DockyardProxyStatus SendSampleList(const SampleList list) override;

  // |DockyardProxy|.
  virtual DockyardProxyStatus SendStringSampleList(
      const StringSampleList list) override;

 private:
  // A local stub for the remote Dockyard instance.
  std::unique_ptr<dockyard_proto::Dockyard::Stub> stub_;
  // For looking up the ID of a Dockyard path.
  std::map<std::string, dockyard::DockyardId> dockyard_path_to_id_;

  // Actually send data to the Dockyard.
  // |time| is in nanoseconds.
  // See also: SendInspectJson().
  grpc::Status SendInspectJsonById(uint64_t time,
                                   dockyard::DockyardId dockyard_id,
                                   const std::string& json);

  // Actually send a single sample to the Dockyard.
  // |time| is in nanoseconds.
  // See also: SendSample().
  grpc::Status SendSampleById(uint64_t time, dockyard::DockyardId dockyard_id,
                              uint64_t value);

  // Actually send a list of samples with the same timestamp to the Dockyard.
  // |time| is in nanoseconds.
  // See also: SendSampleList().
  grpc::Status SendSampleListById(uint64_t time, const SampleListById list);

  // Get the ID from the local cache or from the remote Dockyard if it's not in
  // the cache.
  grpc::Status GetDockyardIdForPath(dockyard::DockyardId* dockyard_id,
                                    const std::string& dockyard_path);
  // As above, but for a list of paths and IDs.
  grpc::Status GetDockyardIdsForPaths(
      std::vector<dockyard::DockyardId>* dockyard_id,
      const std::vector<const std::string*>& dockyard_path);
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_GRPC_H_
