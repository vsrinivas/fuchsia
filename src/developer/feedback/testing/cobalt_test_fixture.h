// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_TESTING_COBALT_TEST_FIXTURE_H_
#define SRC_DEVELOPER_FEEDBACK_TESTING_COBALT_TEST_FIXTURE H_

#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>

#include <memory>

#include "sdk/lib/sys/cpp/service_directory.h"
#include "src/developer/feedback/testing/stubs/stub_cobalt_logger_factory.h"
#include "src/developer/feedback/testing/unit_test_fixture.h"
#include "src/developer/feedback/utils/cobalt.h"
#include "src/lib/fxl/logging.h"

namespace feedback {

class CobaltTestFixture {
 public:
  CobaltTestFixture(UnitTestFixture* unit_test_fixture) : unit_test_fixture_(unit_test_fixture) {}

 protected:
  void SetUpCobaltLoggerFactory(std::unique_ptr<StubCobaltLoggerFactoryBase> logger_factory) {
    logger_factory_ = std::move(logger_factory);
    if (logger_factory_ && unit_test_fixture_) {
      unit_test_fixture_->InjectServiceProvider(logger_factory_.get());
    }
  }

  const std::vector<CobaltEvent>& ReceivedCobaltEvents() const { return logger_factory_->Events(); }

  bool WasLogEventCalled() { return logger_factory_->WasLogEventCalled(); }
  bool WasLogEventCountCalled() { return logger_factory_->WasLogEventCountCalled(); }

  void CloseFactoryConnection() { logger_factory_->CloseFactoryConnection(); }
  void CloseLoggerConnection() { logger_factory_->CloseLoggerConnection(); }

 private:
  std::unique_ptr<StubCobaltLoggerFactoryBase> logger_factory_;
  UnitTestFixture* unit_test_fixture_;
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_TESTING_COBALT_TEST_FIXTURE_H_
