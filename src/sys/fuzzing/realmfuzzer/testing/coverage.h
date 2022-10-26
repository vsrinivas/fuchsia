// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_TESTING_COVERAGE_H_
#define SRC_SYS_FUZZING_REALMFUZZER_TESTING_COVERAGE_H_

#include <fuchsia/debugdata/cpp/fidl.h>
#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/zx/eventpair.h>
#include <lib/zx/vmo.h>

#include <string>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/realmfuzzer/engine/coverage-data.h"

namespace fuzzing {

using fuchsia::debugdata::Publisher;
using fuchsia::fuzzer::CoverageData;
using fuchsia::fuzzer::CoverageDataCollector;
using fuchsia::fuzzer::CoverageDataProvider;
using fuchsia::fuzzer::InstrumentedProcess;

// This class represents a simplified fuzz coverage component. Unlike the real version (located at
// src/sys/test_manager/fuzz_coverage), this version for testing accepts only a single collector
// connection and a single provider connection, and does not use event streams.
class FakeCoverage final : public CoverageDataCollector, CoverageDataProvider {
 public:
  explicit FakeCoverage(ExecutorPtr executor);
  ~FakeCoverage() = default;

  fidl::InterfaceRequestHandler<Publisher> GetPublisherHandler();
  fidl::InterfaceRequestHandler<CoverageDataCollector> GetCollectorHandler();
  fidl::InterfaceRequestHandler<CoverageDataProvider> GetProviderHandler();

  // CoverageDataCollector FIDL methods.
  void Initialize(InstrumentedProcess instrumented, InitializeCallback callback) override;
  void AddLlvmModule(zx::vmo inline_8bit_counters, AddLlvmModuleCallback callback) override;

  // CoverageDataProvider FIDL method.
  void SetOptions(Options options) override;
  void GetCoverageData(GetCoverageDataCallback callback) override;

 private:
  fidl::Binding<CoverageDataCollector> collector_;
  fidl::Binding<CoverageDataProvider> provider_;
  ExecutorPtr executor_;
  Options options_;
  AsyncSender<CoverageData> sender_;
  AsyncReceiver<CoverageData> receiver_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(FakeCoverage);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_TESTING_COVERAGE_H_
