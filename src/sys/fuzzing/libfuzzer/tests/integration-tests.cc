// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/tests/integration-tests.h"

namespace fuzzing {

// Test fixtures.

// libFuzzer normally attaches to itself as a debugger to catch crashes; but can be prevented from
// doing so when another debugger like zxdb is needed to investigate failed tests.
#define LIBFUZZER_ALLOW_DEBUG 0

class LibFuzzerTest : public EngineIntegrationTest {
 protected:
  std::string program_binary() const override { return "bin/libfuzzer_engine"; }

  std::string component_url() const override {
    return "fuchsia-pkg://fuchsia.com/libfuzzer-integration-tests#meta/fake.cm";
  }

  std::vector<std::string> extra_args() const override {
    return {
        "bin/libfuzzer_test_fuzzer",
    };
  }

  zx::channel fuzz_coverage() override {
    // The libfuzzer engine doesn't use published debug data, so this can just be a dummy channel.
    zx::channel channel;
    if (auto status = zx::channel::create(0, &channel_, &channel); status != ZX_OK) {
      FX_LOGS(FATAL) << "Failed to create channel: " << zx_status_get_string(status);
    }
    return channel;
  }

  void set_options(Options& options) const override {
    // See notes on LIBFUZZER_ALLOW_DEBUG above.
    options.set_debug(LIBFUZZER_ALLOW_DEBUG);
  }

 private:
  zx::channel channel_;
};

#undef LIBFUZZER_ALLOW_DEBUG

// Integration tests.

#define ENGINE_INTEGRATION_TEST LibFuzzerTest
#include "src/sys/fuzzing/common/tests/integration-tests.inc"
#undef ENGINE_INTEGRATION_TEST

}  // namespace fuzzing
