// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_CRASH_LISTENER_H_
#define SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_CRASH_LISTENER_H_

#include "src/virtualization/third_party/vm_tools/vm_crash.grpc.pb.h"

namespace linux_runner {

class CrashListener : public vm_tools::cicerone::CrashListener::Service {
 public:
  ~CrashListener() override = default;

 private:
  // |vm_tools::cicerone::CrashListener::Service|
  grpc::Status CheckMetricsConsent(
      ::grpc::ServerContext* context, const ::vm_tools::EmptyMessage* request,
      ::vm_tools::cicerone::MetricsConsentResponse* response) override {
    return grpc::Status::OK;
  }
  grpc::Status SendCrashReport(::grpc::ServerContext* context,
                               const ::vm_tools::cicerone::CrashReport* request,
                               ::vm_tools::EmptyMessage* response) override {
    return grpc::Status::OK;
  }
  grpc::Status SendFailureReport(::grpc::ServerContext* context,
                                 const ::vm_tools::cicerone::FailureReport* request,
                                 ::vm_tools::EmptyMessage* response) override {
    return grpc::Status::OK;
  }
};

}  // namespace linux_runner

#endif  // SRC_VIRTUALIZATION_BIN_LINUX_RUNNER_CRASH_LISTENER_H_
