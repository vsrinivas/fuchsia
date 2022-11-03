// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_TESTS_INTEGRATION_TESTS_H_
#define SRC_SYS_FUZZING_COMMON_TESTS_INTEGRATION_TESTS_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/process.h>
#include <zircon/status.h>

#include <memory>
#include <vector>

#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/child-process.h"
#include "src/sys/fuzzing/common/component-context.h"
#include "src/sys/fuzzing/common/controller-provider.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/common/testing/registrar.h"

namespace fuzzing {

using fuchsia::fuzzer::ControllerProviderPtr;
using fuchsia::fuzzer::ControllerPtr;
using fuchsia::fuzzer::Registrar;

// The |EngineIntegrationTest| fakes the registrar but uses a real fuzzing engine.
//
class EngineIntegrationTest : public AsyncTest {
 protected:
  void SetUp() override;

  ComponentContext* context() { return context_.get(); }

  // Returns the path to the binary relative to "/pkg".
  virtual std::string program_binary() const = 0;

  // Returns the URL of the component that owns the binary.
  virtual std::string component_url() const = 0;

  // Returns any additional command line arguments.
  virtual std::vector<std::string> extra_args() const = 0;

  // Returns the channel to the debug data service for fuzzer coverage.
  virtual zx::channel fuzz_coverage() = 0;

  // Set the options to configure the controller with.
  virtual void set_options(Options& options) const = 0;

  // Creates fake registry and coverage components, and spawns the engine.
  ZxPromise<ControllerPtr> Start();

  void TearDown() override;

  // Integration tests.

  void Crash();

 private:
  ComponentContextPtr context_;
  std::unique_ptr<ChildProcess> engine_;
  ControllerProviderPtr provider_;
  std::unique_ptr<FakeRegistrar> registrar_;
  Scope scope_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_TESTS_INTEGRATION_TESTS_H_
