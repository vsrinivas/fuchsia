// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_REALMFUZZER_ENGINE_PROCESS_PROXY_TEST_H_
#define SRC_SYS_FUZZING_REALMFUZZER_ENGINE_PROCESS_PROXY_TEST_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <stdint.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/sys/fuzzing/common/async-eventpair.h"
#include "src/sys/fuzzing/common/options.h"
#include "src/sys/fuzzing/common/testing/async-test.h"
#include "src/sys/fuzzing/realmfuzzer/engine/module-pool.h"
#include "src/sys/fuzzing/realmfuzzer/engine/process-proxy.h"
#include "src/sys/fuzzing/realmfuzzer/testing/target.h"

namespace fuzzing {

// Base class for ProcessProxy unit tests. This is in its own compilation unit so it can be used by
// both the normal unit tests, and the unit tests that produce fatal logs.
//
// The |Ignore...| methods and functions are useful for creating objects needed to make FIDL calls
// but that are otherwise irrelevant to a particular tests.
class ProcessProxyTest : public AsyncTest {
 protected:
  void SetUp() override;

  ModulePoolPtr pool() const { return pool_; }

  // Creates a proxy. Configures the proxy with default options, connects it to the given |process|,
  // and waits for the proxy to acknowledge the connection.
  std::unique_ptr<ProcessProxy> CreateAndConnectProxy(zx::process process);

  // Like |CreateAndConnectProxy| above, but uses the given options instead of the defaults.
  std::unique_ptr<ProcessProxy> CreateAndConnectProxy(zx::process process,
                                                      const OptionsPtr& options);

  // Like |CreateAndConnectProxy| above, but uses the given |eventpair| instead of a temporary one.
  std::unique_ptr<ProcessProxy> CreateAndConnectProxy(zx::process process, zx::eventpair eventpair);

 private:
  std::unique_ptr<ProcessProxy> CreateAndConnectProxy(zx::process process,
                                                      const OptionsPtr& options,
                                                      zx::eventpair eventpair);

  ModulePoolPtr pool_;
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_REALMFUZZER_ENGINE_PROCESS_PROXY_TEST_H_
