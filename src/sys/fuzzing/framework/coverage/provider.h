// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_PROVIDER_H_
#define SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_PROVIDER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>

#include <atomic>
#include <memory>
#include <thread>

#include "src/lib/fxl/synchronization/thread_annotations.h"
#include "src/sys/fuzzing/common/binding.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/sync-wait.h"
#include "src/sys/fuzzing/framework/coverage/event-queue.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoverageProvider;

class CoverageProviderImpl : public CoverageProvider {
 public:
  CoverageProviderImpl(std::shared_ptr<CoverageEventQueue> events);
  ~CoverageProviderImpl() override;

  // FIDL methods.
  fidl::InterfaceRequestHandler<CoverageProvider> GetHandler();
  void SetOptions(Options options) override;
  void WatchCoverageEvent(WatchCoverageEventCallback callback) override FXL_LOCKS_EXCLUDED(mutex_);

  // Blocks until the engine connects to this provider.
  void AwaitConnect();

  // Blocks until the engine closes the underlying channel.
  void AwaitClose();

 private:
  Binding<CoverageProvider> binding_;
  std::shared_ptr<CoverageEventQueue> events_;
  SyncWait connect_;
  SyncWait request_;
  std::thread loop_;
  std::mutex mutex_;
  WatchCoverageEventCallback callback_ FXL_GUARDED_BY(mutex_);
  std::atomic<bool> closing_ = false;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(CoverageProviderImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_PROVIDER_H_
