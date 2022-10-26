// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_ENGINE_COVERAGE_DATA_PROVIDER_CLIENT_H_
#define SRC_SYS_FUZZING_REALMFUZZER_ENGINE_COVERAGE_DATA_PROVIDER_CLIENT_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <stdint.h>
#include <zircon/compiler.h>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/realmfuzzer/engine/module-proxy.h"

namespace fuzzing {

using fuchsia::fuzzer::CoverageData;
using fuchsia::fuzzer::CoverageDataProviderPtr;

// This class encapsulates a client of |fuchsia.fuzzer.CoverageDataProvider|.
class CoverageDataProviderClient final {
 public:
  explicit CoverageDataProviderClient(ExecutorPtr executor);
  ~CoverageDataProviderClient() = default;

  void Configure(const OptionsPtr& options);

  // FIDL methods.
  __WARN_UNUSED_RESULT zx_status_t Bind(zx::channel channel);
  Promise<CoverageData> GetCoverageData();

 private:
  ExecutorPtr executor_;
  OptionsPtr options_;
  CoverageDataProviderPtr provider_;
  AsyncSender<CoverageData> sender_;
  AsyncReceiver<CoverageData> receiver_;
  Scope scope_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(CoverageDataProviderClient);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_ENGINE_COVERAGE_DATA_PROVIDER_CLIENT_H_
