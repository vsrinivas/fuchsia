// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_GRPC_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_GRPC_H_

#include "dockyard_proxy.h"
#include "src/developer/system_monitor/lib/dockyard/dockyard.h"
#include "src/developer/system_monitor/lib/proto/dockyard.grpc.pb.h"

#include <grpc++/grpc++.h>

namespace harvester {

namespace internal {

// Utility functions for munging data.

void ExtractPathsFromSampleList(
    std::vector<const std::string*>* dockyard_strings, const SampleList& list);

void BuildSampleListById(SampleListById* by_id,
                         const std::vector<dockyard::DockyardId>& id_list,
                         const SampleList& sample_list);

}  // namespace internal

class DockyardProxyGrpc : public DockyardProxy {
 public:
  DockyardProxyGrpc(std::shared_ptr<grpc::Channel> channel)
      : stub_(dockyard_proto::Dockyard::NewStub(channel)) {}

  // |DockyardProxy|.
  DockyardProxyStatus Init() override;

  // |DockyardProxy|.
  DockyardProxyStatus SendInspectJson(const std::string& dockyard_path,
                                      const std::string& json) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendSample(const std::string& dockyard_path,
                                 uint64_t value) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendSampleList(const SampleList& list) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendStringSampleList(
      const StringSampleList& list) override;

  // |DockyardProxy|.
  DockyardProxyStatus SendSamples(
      const SampleList& int_samples,
      const StringSampleList& string_samples) override;

 private:
  // A local stub for the remote Dockyard instance.
  std::unique_ptr<dockyard_proto::Dockyard::Stub> stub_;

  // The dockyard_path_to_id_ may be accessed by multiple threads.
  std::mutex dockyard_path_to_id_mutex_;
  // Look up the ID of a Dockyard path.
  std::map<std::string, dockyard::DockyardId> dockyard_path_to_id_ = {};

  // Functions for interacting with Dockyard (via gRPC).

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
  grpc::Status SendSampleListById(uint64_t time, const SampleListById& list);

  // Get the ID from the local cache or from the remote Dockyard if it's not in
  // the cache.
  grpc::Status GetDockyardIdForPath(dockyard::DockyardId* dockyard_id,
                                    const std::string& dockyard_path);
  // As above, for a list of paths and IDs.
  grpc::Status GetDockyardIdsForPaths(
      std::vector<dockyard::DockyardId>* dockyard_id,
      const std::vector<const std::string*>& dockyard_path);
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_DOCKYARD_PROXY_GRPC_H_
