// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include <lib/syslog/cpp/logger.h>

#include <gtest/gtest.h>

#include "src/developer/exception_broker/exception_broker.h"
#include "src/developer/exception_broker/tests/crasher_wrapper.h"
#include "src/lib/fxl/test/test_settings.h"

namespace fuchsia {
namespace exception {
namespace {

bool GetProcessException(ProcessException* pe) {
  // Create a process that crashes and obtain the relevant handles and exception.
  // By the time |SpawnCrasher| has returned, the process has already thrown an exception.
  if (!SpawnCrasher(pe))
    return false;

  // We mark the exception to be handled. We need this because we pass on the exception to the
  // handler, which will resume it before we get the control back. If we don't mark it as handled,
  // the exception will bubble out of our environment.
  uint32_t state = ZX_EXCEPTION_STATE_HANDLED;
  if (zx_status_t res = pe->exception.set_property(ZX_PROP_EXCEPTION_STATE, &state, sizeof(state));
      res != ZX_OK) {
    FX_PLOGS(ERROR, res) << "Could not set handled state to exception.";
    return false;
  }

  return true;
}

ExceptionInfo ProcessExceptionToExceptionInfo(const ProcessException& pe) {
  // Translate the exception to the fidl format.
  ExceptionInfo exception_info;
  exception_info.process_koid = pe.exception_info.pid;
  exception_info.thread_koid = pe.exception_info.tid;
  exception_info.type = static_cast<ExceptionType>(pe.exception_info.type);

  return exception_info;
}

TEST(ExceptionBrokerIntegrationTest, SmokeTest) {
  ProcessException pe;
  ASSERT_TRUE(GetProcessException(&pe));

  fuchsia::exception::HandlerSyncPtr exception_handler;

  auto environment_services = sys::ServiceDirectory::CreateFromNamespace();
  environment_services->Connect(exception_handler.NewRequest());

  ASSERT_EQ(
      exception_handler->OnException(std::move(pe.exception), ProcessExceptionToExceptionInfo(pe)),
      ZX_OK);
}

}  // namespace
}  // namespace exception
}  // namespace fuchsia

int main(int argc, char* argv[]) {
  if (!fxl::SetTestSettings(argc, argv))
    return EXIT_FAILURE;

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"exception-broker", "integration-test"});

  return RUN_ALL_TESTS();
}
