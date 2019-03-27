// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_GRPC_H_
#define GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_GRPC_H_

#include <grpc++/grpc++.h>

#include "garnet/lib/system_monitor/dockyard/dockyard.h"
#include "garnet/lib/system_monitor/protos/dockyard.grpc.pb.h"
#include "harvester.h"

namespace harvester {

class HarvesterGrpc : public Harvester {
 public:
  HarvesterGrpc(std::shared_ptr<grpc::Channel> channel)
      : stub_(dockyard_proto::Dockyard::NewStub(channel)) {}

  // |Harvester|.
  virtual HarvesterStatus Init() override;

  // |Harvester|.
  virtual HarvesterStatus SendInspectJson(const std::string& stream_name,
                                          const std::string& json) override;

  // |Harvester|.
  virtual HarvesterStatus SendSample(const std::string& stream_name,
                                     uint64_t value) override;

  // |Harvester|.
  virtual HarvesterStatus SendSampleList(const SampleList list) override;

 private:
  // A local stub for the remote Dockyard instance.
  std::unique_ptr<dockyard_proto::Dockyard::Stub> stub_;
  // For looking up the ID of a stream name.
  std::map<std::string, dockyard::SampleStreamId> stream_ids_;

  // Actually send data to the Dockyard.
  // |time| is in nanoseconds.
  // See also: SendInspectJson().
  grpc::Status SendInspectJsonById(uint64_t time,
                                   dockyard::SampleStreamId stream_id,
                                   const std::string& json);

  // Actually send a single sample to the Dockyard.
  // |time| is in nanoseconds.
  // See also: SendSample().
  grpc::Status SendSampleById(uint64_t time, dockyard::SampleStreamId stream_id,
                              uint64_t value);

  // Actually send a list of samples with the same timestamp to the Dockyard.
  // |time| is in nanoseconds.
  // See also: SendSampleList().
  grpc::Status SendSampleListById(uint64_t time, const SampleListById list);

  // Get the ID from the local cache or from the remote Dockyard if it's not in
  // the cache.
  grpc::Status GetStreamIdForName(dockyard::SampleStreamId* stream_id,
                                  const std::string& stream_name);
};

}  // namespace harvester

#endif  // GARNET_BIN_SYSTEM_MONITOR_HARVESTER_HARVESTER_GRPC_H_
