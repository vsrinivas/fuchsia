// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/tests/integration-tests.h"

#include <fuchsia/fuzzer/cpp/fidl.h>

namespace fuzzing {

using fuchsia::fuzzer::CoverageDataProvider;

// Test fixtures.

class RealmFuzzerTest : public EngineIntegrationTest {
 protected:
  std::string program_binary() const override { return "bin/realmfuzzer_engine"; }

  std::string component_url() const override {
    return "fuchsia-pkg://fuchsia.com/realmfuzzer-integration-tests#meta/fake.cm";
  }

  std::vector<std::string> extra_args() const override { return {}; }

  zx::channel fuzz_coverage() override {
    fidl::InterfaceHandle<CoverageDataProvider> provider_handle;
    if (auto status = context()->Connect(provider_handle.NewRequest()); status != ZX_OK) {
      FX_LOGS(FATAL) << "Failed to connect to fuzz_coverage: " << zx_status_get_string(status);
    }
    return provider_handle.TakeChannel();
  }

  void set_options(Options& options) const override {}
};

// Integration tests.

#define ENGINE_INTEGRATION_TEST RealmFuzzerTest
#include "src/sys/fuzzing/common/tests/integration-tests.inc"
#undef ENGINE_INTEGRATION_TEST

}  // namespace fuzzing
