// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <grpc++/grpc++.h>
#include "garnet/lib/system_monitor/protos/dockyard.grpc.pb.h"
#include "garnet/lib/system_monitor/dockyard/dockyard.h"

class Harvester {
 public:
  Harvester(std::shared_ptr<grpc::Channel> channel)
      : stub_(dockyard_proto::Dockyard::NewStub(channel)) {}

  // Initialize the Harvester.
  bool Init();

  // Send a single sample to the Dockyard.
  grpc::Status SendSample(const std::string& stream_name, uint64_t value);

 private:
  // A local stub for the remote Dockyard instance.
  std::unique_ptr<dockyard_proto::Dockyard::Stub> stub_;
  // For looking up the ID of a stream name.
  std::map<std::string, dockyard::SampleStreamId> stream_ids_;

  // Actually send a single sample to the Dockyard.
  // See also: SendSample().
  grpc::Status SendSampleById(uint64_t time, dockyard::SampleStreamId stream_id,
                              uint64_t value);

  // Get the ID from the local cache or from the remote Dockyard if it's not in
  // the cache.
  grpc::Status GetStreamIdForName(dockyard::SampleStreamId* stream_id,
                                  const std::string& stream_name);
};
