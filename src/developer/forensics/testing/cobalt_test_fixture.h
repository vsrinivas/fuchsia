// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_COBALT_TEST_FIXTURE_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_COBALT_TEST_FIXTURE_H_

#include <lib/async/dispatcher.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/testing/unit_test_fixture.h"
#include "src/developer/forensics/utils/cobalt/logger.h"

namespace forensics {

class CobaltTestFixture {
 public:
  CobaltTestFixture(UnitTestFixture* unit_test_fixture) : unit_test_fixture_(unit_test_fixture) {}

 protected:
  void SetUpCobaltServer(std::unique_ptr<stubs::CobaltLoggerFactoryBase> server) {
    logger_factory_server_ = std::move(server);
    if (logger_factory_server_ && unit_test_fixture_) {
      unit_test_fixture_->InjectServiceProvider(logger_factory_server_.get());
    }
  }

  const std::vector<cobalt::Event>& ReceivedCobaltEvents() const {
    return logger_factory_server_->Events();
  }

  bool WasLogEventCalled() { return logger_factory_server_->WasLogEventCalled(); }
  bool WasLogEventCountCalled() { return logger_factory_server_->WasLogEventCountCalled(); }

  void CloseFactoryConnection() { logger_factory_server_->CloseConnection(); }
  void CloseLoggerConnection() { logger_factory_server_->CloseLoggerConnection(); }

 private:
  std::unique_ptr<stubs::CobaltLoggerFactoryBase> logger_factory_server_;
  UnitTestFixture* unit_test_fixture_;
};

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_COBALT_TEST_FIXTURE_H_
