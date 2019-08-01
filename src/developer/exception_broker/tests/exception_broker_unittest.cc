// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/exception_broker/exception_broker.h"

#include <fuchsia/crash/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <lib/syslog/cpp/logger.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include <gtest/gtest.h>

#include "src/developer/exception_broker/tests/crasher_wrapper.h"
#include "src/lib/fxl/test/test_settings.h"

namespace fuchsia {
namespace exception {
namespace {

// ExceptionBroker unit test -----------------------------------------------------------------------
//
// This test is meant to verify that the exception broker does the correct thing depending on the
// configuration. The main objective of this test is to verify that the connected analyzers and
// exception handlers actually receive the exception from the broker.

// Stub implementation of Analyzer -----------------------------------------------------------------

class StubAnalyzer : public fuchsia::crash::Analyzer {
 public:
  // |fuchsia::crash::Analyzer|
  //
  // ExceptionBroker only talks native exceptions.
  void OnNativeException(zx::process process, zx::thread thread,
                         OnNativeExceptionCallback callback) override {
    native_exception_count_++;

    fuchsia::crash::Analyzer_OnNativeException_Result result;
    result.set_response({});
    callback(std::move(result));
  }

  void OnManagedRuntimeException(std::string component_url,
                                 fuchsia::crash::ManagedRuntimeException exception,
                                 OnManagedRuntimeExceptionCallback callback) override {
    FXL_NOTIMPLEMENTED() << "Exception broker does not call this method.";
  }
  void OnKernelPanicCrashLog(fuchsia::mem::Buffer crash_log,
                             OnKernelPanicCrashLogCallback callback) override {
    FXL_NOTIMPLEMENTED() << "Exception broker does not call this method.";
  }

  fidl::InterfaceRequestHandler<fuchsia::crash::Analyzer> GetHandler() {
    return [this](fidl::InterfaceRequest<fuchsia::crash::Analyzer> request) {
      total_connections_++;
      bindings_.AddBinding(this, std::move(request));
    };
  }

  // Getters.

  int native_exception_count() const { return native_exception_count_; }
  int total_connections() const { return total_connections_; }

 private:
  int native_exception_count_ = 0;

  fidl::BindingSet<fuchsia::crash::Analyzer> bindings_;
  int total_connections_ = 0;
};

// Test Setup --------------------------------------------------------------------------------------
//
// Necessary elements for a fidl test to run. The ServiceDirectoryProvider is meant to mock the
// environment from which a process gets its services. This is the way we "inject" in our stub
// analyzer instead of the real one.

struct TestContext {
  async::Loop loop;
  sys::testing::ServiceDirectoryProvider services;
  std::unique_ptr<StubAnalyzer> analyzer;
};

std::unique_ptr<TestContext> CreateTestContext() {
  std::unique_ptr<TestContext> context(
      new TestContext{.loop = async::Loop(&kAsyncLoopConfigAttachToThread),
                      .services = sys::testing::ServiceDirectoryProvider{},
                      .analyzer = std::make_unique<StubAnalyzer>()});

  return context;
}

// Runs a loop until |condition| is true. Does this by stopping every |step| to check the condition.
// If |condition| is never true, the thread will never leave this cycle.
// The test harness has to be able to handle this "hanging" case.
void RunUntil(TestContext* context, fit::function<bool()> condition,
              zx::duration step = zx::msec(10)) {
  while (!condition()) {
    context->loop.Run(zx::deadline_after(step));
  }
}

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

// Tests -------------------------------------------------------------------------------------------

TEST(ExceptionBrokerUnittest, CallingMultipleExceptions) {
  auto test_context = CreateTestContext();

  // We add the service we're injecting.
  test_context->services.AddService(test_context->analyzer->GetHandler());

  auto broker = ExceptionBroker::Create(test_context->loop.dispatcher(),
                                        test_context->services.service_directory());
  ASSERT_TRUE(broker);

  // We create multiple exceptions.
  ProcessException excps[3];
  ASSERT_TRUE(GetProcessException(excps + 0));
  ASSERT_TRUE(GetProcessException(excps + 1));
  ASSERT_TRUE(GetProcessException(excps + 2));

  // Get the fidl representation of the exception.
  ExceptionInfo infos[3];
  infos[0] = ProcessExceptionToExceptionInfo(excps[0]);
  infos[1] = ProcessExceptionToExceptionInfo(excps[1]);
  infos[2] = ProcessExceptionToExceptionInfo(excps[2]);

  // It's not easy to pass array references to lambdas.
  bool cb_call0 = false;
  bool cb_call1 = false;
  bool cb_call2 = false;
  broker->OnException(std::move(excps[0].exception), infos[0], [&cb_call0]() { cb_call0 = true; });
  broker->OnException(std::move(excps[1].exception), infos[1], [&cb_call1]() { cb_call1 = true; });
  broker->OnException(std::move(excps[2].exception), infos[2], [&cb_call2]() { cb_call2 = true; });

  // There should be many connections opened.
  ASSERT_EQ(broker->connections().size(), 3u);

  // We wait until the analyzer has received an exception.
  RunUntil(test_context.get(),
           [&test_context]() { return test_context->analyzer->native_exception_count() == 3; });

  EXPECT_TRUE(cb_call0);
  EXPECT_TRUE(cb_call1);
  EXPECT_TRUE(cb_call2);

  // All connections should be killed now.
  EXPECT_EQ(broker->connections().size(), 0u);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  excps[0].job.kill();
  excps[1].job.kill();
  excps[2].job.kill();
}

TEST(ExceptionBrokerUnittest, NoConnection) {
  // We don't inject a stub service. This will make connecting to the service fail.
  auto test_context = CreateTestContext();

  auto broker = ExceptionBroker::Create(test_context->loop.dispatcher(),
                                        test_context->services.service_directory());
  ASSERT_TRUE(broker);

  // Create the exception.
  ProcessException exception;
  ASSERT_TRUE(GetProcessException(&exception));
  ExceptionInfo info = ProcessExceptionToExceptionInfo(exception);

  bool called = false;
  broker->OnException(std::move(exception.exception), info, [&called]() { called = true; });

  // There should be an outgoing connection.
  ASSERT_EQ(broker->connections().size(), 1u);

  RunUntil(test_context.get(), [&broker]() { return broker->connections().empty(); });
  ASSERT_TRUE(called);

  // The stub shouldn't be called.
  ASSERT_EQ(test_context->analyzer->native_exception_count(), 0);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  exception.job.kill();
}

}  // namespace
}  // namespace exception
}  // namespace fuchsia

int main(int argc, char* argv[]) {
  if (!fxl::SetTestSettings(argc, argv))
    return EXIT_FAILURE;

  testing::InitGoogleTest(&argc, argv);
  syslog::InitLogger({"exception-broker", "unittest"});

  return RUN_ALL_TESTS();
}
