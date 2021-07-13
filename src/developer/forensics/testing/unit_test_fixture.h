// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_TESTING_UNIT_TEST_FIXTURE_H_
#define SRC_DEVELOPER_FORENSICS_TESTING_UNIT_TEST_FIXTURE_H_

#include <lib/async/dispatcher.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/inspect/cpp/hierarchy.h>
#include <lib/inspect/cpp/inspect.h>
#include <lib/inspect/cpp/reader.h>
#include <lib/inspect/testing/cpp/inspect.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/developer/forensics/testing/stubs/cobalt_logger_factory.h"
#include "src/developer/forensics/utils/cobalt/logger.h"
#include "src/lib/testing/loop_fixture/test_loop_fixture.h"

namespace forensics {

class UnitTestFixture : public gtest::TestLoopFixture {
 public:
  UnitTestFixture() : service_directory_provider_(dispatcher()) {}

 protected:
  std::shared_ptr<sys::ServiceDirectory>& services() {
    return service_directory_provider_.service_directory();
  }

  template <typename Protocol>
  void AddHandler(::fidl::InterfaceRequestHandler<Protocol> handler, const char* name = nullptr) {
    if (name) {
      FX_CHECK(service_directory_provider_.AddService(std::move(handler), name) == ZX_OK);
    } else {
      FX_CHECK(service_directory_provider_.AddService(std::move(handler)) == ZX_OK);
    }
  }

  template <typename ServiceProvider>
  void InjectServiceProvider(ServiceProvider* service_provider, const char* name = nullptr) {
    AddHandler(service_provider->GetHandler(), name);
  }

  // Inspect related methods.
  inspect::Node& InspectRoot() const { return inspector_.GetRoot(); }

  inspect::Hierarchy InspectTree() {
    auto result = inspect::ReadFromVmo(inspector_.DuplicateVmo());
    FX_CHECK(result.is_ok());
    return result.take_value();
  }

  // Cobalt related methods
  void SetUpCobaltServer(std::unique_ptr<stubs::CobaltLoggerFactoryBase> server) {
    logger_factory_server_ = std::move(server);
    if (logger_factory_server_) {
      InjectServiceProvider(logger_factory_server_.get());
    }
  }

  const std::vector<cobalt::Event>& ReceivedCobaltEvents() const {
    return logger_factory_server_->Events();
  }

  bool WasMethodCalled(cobalt::EventType name) const {
    return logger_factory_server_->WasMethodCalled(name);
  }

  void CloseFactoryConnection() { logger_factory_server_->CloseConnection(); }
  void CloseLoggerConnection() { logger_factory_server_->CloseLoggerConnection(); }

 private:
  sys::testing::ServiceDirectoryProvider service_directory_provider_;
  inspect::Inspector inspector_;

  std::unique_ptr<stubs::CobaltLoggerFactoryBase> logger_factory_server_;
};

}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_TESTING_UNIT_TEST_FIXTURE_H_
