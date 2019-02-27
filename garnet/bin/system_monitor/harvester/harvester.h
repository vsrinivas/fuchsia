// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_

#include <grpc++/grpc++.h>
#include <zircon/types.h>

#include "garnet/lib/system_monitor/dockyard/dockyard.h"
#include "garnet/lib/system_monitor/protos/dockyard.grpc.pb.h"

namespace harvester {

typedef std::vector<std::pair<const std::string, uint64_t>> SampleList;
typedef std::vector<std::pair<uint64_t, uint64_t>> SampleListById;

class Harvester {
 public:
  Harvester(std::shared_ptr<grpc::Channel> channel)
      : stub_(dockyard_proto::Dockyard::NewStub(channel)) {}

  // Initialize the Harvester.
  bool Init();

  // Send a single sample to the Dockyard.
  grpc::Status SendSample(const std::string& stream_name, uint64_t value);

  // Send a list of samples with the same timestamp to the Dockyard.
  grpc::Status SendSampleList(const SampleList list);

 private:
  // A local stub for the remote Dockyard instance.
  std::unique_ptr<dockyard_proto::Dockyard::Stub> stub_;
  // For looking up the ID of a stream name.
  std::map<std::string, dockyard::SampleStreamId> stream_ids_;

  // Actually send a single sample to the Dockyard.
  // See also: SendSample().
  grpc::Status SendSampleById(uint64_t time, dockyard::SampleStreamId stream_id,
                              uint64_t value);

  // Actually send a list of samples with the same timestamp to the Dockyard.
  // See also: SendSampleList().
  grpc::Status SendSampleListById(uint64_t time, const SampleListById list);

  // Get the ID from the local cache or from the remote Dockyard if it's not in
  // the cache.
  grpc::Status GetStreamIdForName(dockyard::SampleStreamId* stream_id,
                                  const std::string& stream_name);
};

// Gather*Samples collect samples for a given subject. They are grouped to make
// the code more manageable and for enabling/disabling categories in the future.
void GatherCpuSamples(zx_handle_t root_resource, Harvester* harvester);
void GatherMemorySamples(zx_handle_t root_resource, Harvester* harvester);
void GatherThreadSamples(zx_handle_t root_resource, Harvester* harvester);

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_H_
