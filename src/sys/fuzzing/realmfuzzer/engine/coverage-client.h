// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_ENGINE_COVERAGE_CLIENT_H_
#define SRC_SYS_FUZZING_REALMFUZZER_ENGINE_COVERAGE_CLIENT_H_

#include <fuchsia/fuzzer/cpp/fidl.h>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/options.h"

namespace fuzzing {

using fuchsia::fuzzer::CoverageEvent;
using fuchsia::fuzzer::CoverageEventPtr;
using fuchsia::fuzzer::CoverageProvider;
using fuchsia::fuzzer::CoverageProviderPtr;

// This class encapsulates a client of |fuchsia.fuzzer.CoverageProvider|.
class CoverageProviderClient final {
 public:
  explicit CoverageProviderClient(ExecutorPtr executor);
  ~CoverageProviderClient() = default;

  using RequestHandler = fidl::InterfaceRequestHandler<CoverageProvider>;
  void set_handler(RequestHandler handler) { handler_ = std::move(handler); }

  // FIDL methods.
  void SetOptions(const OptionsPtr& options);
  Promise<CoverageEvent> WatchCoverageEvent();

 private:
  // Uses the request handler to connect the client. No-op if already connected.
  void Connect();

  ExecutorPtr executor_;
  RequestHandler handler_;
  CoverageProviderPtr provider_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(CoverageProviderClient);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_ENGINE_COVERAGE_CLIENT_H_
