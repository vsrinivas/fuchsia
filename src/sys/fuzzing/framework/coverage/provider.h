// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_PROVIDER_H_
#define SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_PROVIDER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>

#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

using ::fuchsia::fuzzer::CoverageEvent;
using ::fuchsia::fuzzer::CoverageProvider;

class CoverageProviderImpl : public CoverageProvider {
 public:
  CoverageProviderImpl(ExecutorPtr executor, OptionsPtr options,
                       AsyncDequePtr<CoverageEvent> events);
  ~CoverageProviderImpl() override = default;

  // FIDL methods.
  fidl::InterfaceRequestHandler<CoverageProvider> GetHandler();
  void SetOptions(Options options) override;
  void WatchCoverageEvent(WatchCoverageEventCallback callback) override;

 private:
  fidl::Binding<CoverageProvider> binding_;
  ExecutorPtr executor_;
  OptionsPtr options_;
  AsyncDequePtr<CoverageEvent> events_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(CoverageProviderImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_COVERAGE_PROVIDER_H_
