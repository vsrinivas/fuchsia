// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_MAXWELL_INTEGRATION_CONTEXT_ENGINE_TEST_BASE_H_
#define PERIDOT_TESTS_MAXWELL_INTEGRATION_CONTEXT_ENGINE_TEST_BASE_H_

#include <fuchsia/cpp/modular.h>

#include "peridot/tests/maxwell_integration/test.h"

namespace maxwell {

// Base fixture to support test cases requiring Context Engine.
class ContextEngineTestBase : public MaxwellTestBase {
 public:
  void SetUp() override;

 protected:
  void StartContextAgent(const std::string& url);
  void WaitUntilIdle();

  modular::ContextEngine* context_engine() { return context_engine_.get(); }

 private:
  modular::ContextEnginePtr context_engine_;
  modular::ContextDebugPtr debug_;
};

}  // namespace maxwell

#endif  // PERIDOT_TESTS_MAXWELL_INTEGRATION_CONTEXT_ENGINE_TEST_BASE_H_
