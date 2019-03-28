// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/pkg/biscotti_guest/linux_runner/log_collector.h"

#include <iostream>

namespace linux_runner {

grpc::Status LogCollector::CollectKernelLogs(
    grpc::ServerContext* context, const ::vm_tools::LogRequest* request,
    vm_tools::EmptyMessage* response) {
  return CollectLogs(request);
}

grpc::Status LogCollector::CollectUserLogs(
    grpc::ServerContext* context, const ::vm_tools::LogRequest* request,
    vm_tools::EmptyMessage* response) {
  return CollectLogs(request);
}

grpc::Status LogCollector::CollectLogs(const vm_tools::LogRequest* request) {
  for (int i = 0; i < request->records_size(); ++i) {
    auto record = request->records(i);
    std::cout << record.content();
  }
  return grpc::Status::OK;
}

}  // namespace linux_runner
