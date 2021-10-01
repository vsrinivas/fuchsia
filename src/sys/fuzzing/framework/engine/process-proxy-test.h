// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_PROCESS_PROXY_TEST_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_PROCESS_PROXY_TEST_H_

#include <fuchsia/fuzzer/cpp/fidl.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/dispatcher.h"
#include "src/sys/fuzzing/framework/engine/module-pool.h"
#include "src/sys/fuzzing/framework/engine/process-proxy.h"
#include "src/sys/fuzzing/framework/testing/target.h"

namespace fuzzing {

using ::fuchsia::fuzzer::ProcessProxySyncPtr;

// Base class for ProcessProxy unit tests. This is in its own compilation unit so it can be used by
// both the normal unit tests, and the unit tests that produce fatal logs.
//
// The |Ignore...| methods and functions are useful for creating objects needed to make FIDL calls
// but that are otherwise irrelevant to a particular tests.
class ProcessProxyTest : public ::testing::Test {
 protected:
  void SetUp() override;

  std::shared_ptr<ModulePool> pool() const { return pool_; }

  ProcessProxySyncPtr Bind(ProcessProxyImpl* impl);

  static std::shared_ptr<Options> DefaultOptions();

  zx::eventpair IgnoreSentSignals();
  zx::process IgnoreTarget();
  Options* IgnoreOptions();

 private:
  FakeDispatcher dispatcher_;
  std::shared_ptr<ModulePool> pool_;
  SignalCoordinator coordinator_;
  TestTarget target_;
  Options ignored_;
};

void IgnoreReceivedSignals();
void IgnoreErrors(ProcessProxyImpl* ignored);

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_PROCESS_PROXY_TEST_H_
