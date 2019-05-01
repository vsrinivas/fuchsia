// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_PACKAGES_BISCOTTI_GUEST_LINUX_RUNNER_LOG_COLLECTOR_H_
#define SRC_VIRTUALIZATION_PACKAGES_BISCOTTI_GUEST_LINUX_RUNNER_LOG_COLLECTOR_H_

#include "src/virtualization/packages/biscotti_guest/third_party/protos/vm_host.grpc.pb.h"

namespace linux_runner {

class LogCollector : public vm_tools::LogCollector::Service {
 private:
  // |vm_tools::LogCollector::Service|
  grpc::Status CollectKernelLogs(grpc::ServerContext* context,
                                 const ::vm_tools::LogRequest* request,
                                 vm_tools::EmptyMessage* response) override;
  grpc::Status CollectUserLogs(grpc::ServerContext* context,
                               const ::vm_tools::LogRequest* request,
                               vm_tools::EmptyMessage* response) override;

  grpc::Status CollectLogs(const vm_tools::LogRequest* request);
};

}  // namespace linux_runner

#endif  // SRC_VIRTUALIZATION_PACKAGES_BISCOTTI_GUEST_LINUX_RUNNER_LOG_COLLECTOR_H_
