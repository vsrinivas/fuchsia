// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/termina_guest_manager/log_collector.h"

#include <iostream>

namespace termina_guest_manager {

grpc::Status LogCollector::CollectKernelLogs(grpc::ServerContext* context,
                                             const ::vm_tools::LogRequest* request,
                                             vm_tools::EmptyMessage* response) {
  return CollectLogs(request);
}

grpc::Status LogCollector::CollectUserLogs(grpc::ServerContext* context,
                                           const ::vm_tools::LogRequest* request,
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

}  // namespace termina_guest_manager
