// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/exception_broker/process_limbo_manager.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>

#include <gtest/gtest.h>

#include "src/developer/exception_broker/exception_broker.h"
#include "src/developer/exception_broker/tests/crasher_wrapper.h"
#include "src/lib/fsl/handles/object_info.h"
#include "src/lib/syslog/cpp/logger.h"

namespace fuchsia {
namespace exception {
namespace {

struct TestContext {
  async::Loop loop;
  sys::testing::ServiceDirectoryProvider services;
};

std::unique_ptr<TestContext> CreateTestContext() {
  std::unique_ptr<TestContext> context(new TestContext{
      .loop = async::Loop(&kAsyncLoopConfigAttachToCurrentThread),
      .services = sys::testing::ServiceDirectoryProvider{},
  });

  return context;
}

bool RetrieveExceptionContext(ExceptionContext* pe) {
  // Create a process that crashes and obtain the relevant handles and exception.
  // By the time |SpawnCrasher| has returned, the process has already thrown an exception.
  if (!SpawnCrasher(pe))
    return false;

  // We mark the exception to be handled. We need this because we pass on the exception to the
  // handler, which will resume it before we get the control back. If we don't mark it as handled,
  // the exception will bubble out of our environment.
  return MarkExceptionAsHandled(pe);
}

ExceptionInfo ExceptionContextToExceptionInfo(const ExceptionContext& pe) {
  // Translate the exception to the fidl format.
  ExceptionInfo exception_info;
  exception_info.process_koid = pe.exception_info.pid;
  exception_info.thread_koid = pe.exception_info.tid;
  exception_info.type = static_cast<ExceptionType>(pe.exception_info.type);

  return exception_info;
}

void AddExceptionToLimbo(ProcessLimboManager* limbo_manager, zx::exception exception,
                         ExceptionInfo info) {
  ProcessException process_exception = {};
  process_exception.set_exception(std::move(exception));
  process_exception.set_info(std::move(info));

  zx_status_t status;
  zx::process process;
  status = process_exception.exception().get_process(&process);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Could not obtain process handle for exception.";
  } else {
    process_exception.set_process(std::move(process));
  }

  zx::thread thread;
  status = process_exception.exception().get_thread(&thread);
  if (status != ZX_OK) {
    FX_PLOGS(WARNING, status) << "Could not obtain thread handle for exception.";
  } else {
    process_exception.set_thread(std::move(thread));
  }

  limbo_manager->AddToLimbo(std::move(process_exception));
}

bool ValidateException(const ProcessExceptionMetadata&) { return true; }
bool ValidateException(const ProcessException& exception) { return exception.has_exception(); }

template <typename T>
void ValidateException(const ExceptionContext& context, const T& process_exception) {
  if (std::is_same<T, ProcessException>::value)
    ASSERT_TRUE(ValidateException(process_exception));
  ASSERT_TRUE(process_exception.has_info());
  ASSERT_TRUE(process_exception.has_process());
  ASSERT_TRUE(process_exception.has_thread());

  const zx::process& process = process_exception.process();
  ASSERT_EQ(context.process_koid, fsl::GetKoid(process.get()));
  ASSERT_EQ(context.process_koid, process_exception.info().process_koid);
  ASSERT_EQ(context.process_name, fsl::GetObjectName(process.get()));

  const zx::thread& thread = process_exception.thread();
  ASSERT_EQ(context.thread_koid, fsl::GetKoid(thread.get()));
  ASSERT_EQ(context.thread_koid, process_exception.info().thread_koid);
  ASSERT_EQ(context.thread_name, fsl::GetObjectName(thread.get()));

  ASSERT_EQ(process_exception.info().type, ExceptionType::FATAL_PAGE_FAULT);
}

// Tests -------------------------------------------------------------------------------------------

TEST(ProcessLimboManager, ProcessLimboHandler) {
  ProcessLimboManager limbo_manager;

  // Use the handler interface.
  ProcessLimboHandler limbo_handler(limbo_manager.GetWeakPtr());

  {
    // With no exceptions, it should be empty.
    bool called = false;
    std::vector<ProcessExceptionMetadata> exceptions;
    limbo_handler.ListProcessesWaitingOnException(
        [&called, &exceptions](std::vector<ProcessExceptionMetadata> limbo) {
          called = true;
          exceptions = std::move(limbo);
        });

    ASSERT_TRUE(called);
    ASSERT_EQ(exceptions.size(), 0u);
  }

  // We create multiple exceptions.
  ExceptionContext excps[3];
  ASSERT_TRUE(RetrieveExceptionContext(excps + 0));
  ASSERT_TRUE(RetrieveExceptionContext(excps + 1));
  ASSERT_TRUE(RetrieveExceptionContext(excps + 2));

  // Get the fidl representation of the exception.
  ExceptionInfo infos[3];
  infos[0] = ExceptionContextToExceptionInfo(excps[0]);
  infos[1] = ExceptionContextToExceptionInfo(excps[1]);
  infos[2] = ExceptionContextToExceptionInfo(excps[2]);

  AddExceptionToLimbo(&limbo_manager, std::move(excps[0].exception), infos[0]);
  AddExceptionToLimbo(&limbo_manager, std::move(excps[1].exception), infos[1]);
  AddExceptionToLimbo(&limbo_manager, std::move(excps[2].exception), infos[2]);

  {
    // There should be exceptions listed.
    bool called = false;
    std::vector<ProcessExceptionMetadata> exceptions;
    limbo_handler.ListProcessesWaitingOnException(
        [&called, &exceptions](std::vector<ProcessExceptionMetadata> limbo) {
          called = true;
          exceptions = std::move(limbo);
        });

    ASSERT_TRUE(called);
    ValidateException(excps[0], exceptions[0]);
    ValidateException(excps[1], exceptions[1]);
    ValidateException(excps[2], exceptions[2]);
  }

  {
    // Getting a exception for a process that doesn't exist should fail.
    bool called = false;
    ProcessLimbo_RetrieveException_Result result;
    limbo_handler.RetrieveException(-1,
                                    [&called, &result](ProcessLimbo_RetrieveException_Result res) {
                                      called = true;
                                      result = std::move(res);
                                    });
    ASSERT_TRUE(called);
    ASSERT_TRUE(result.is_err());

    // There should still be 3 exceptions.
    ASSERT_EQ(limbo_manager.limbo().size(), 3u);
  }

  {
    // Getting an actual exception should work.
    bool called = false;
    ProcessLimbo_RetrieveException_Result result = {};
    limbo_handler.RetrieveException(infos[0].process_koid,
                                    [&called, &result](ProcessLimbo_RetrieveException_Result res) {
                                      called = true;
                                      result = std::move(res);
                                    });
    ASSERT_TRUE(called);
    ASSERT_TRUE(result.is_response());
    ValidateException(excps[0], result.response().process_exception);

    // There should be one less exception.
    ASSERT_EQ(limbo_manager.limbo().size(), 2u);
  }

  {
    // That process should have been removed.
    bool called = false;
    ProcessLimbo_RetrieveException_Result result = {};
    limbo_handler.RetrieveException(infos[0].process_koid,
                                    [&called, &result](ProcessLimbo_RetrieveException_Result res) {
                                      called = true;
                                      result = std::move(res);
                                    });
    ASSERT_TRUE(called);
    ASSERT_TRUE(result.is_err());
  }

  {
    // Asking for the other process should work.
    bool called = false;
    ProcessLimbo_RetrieveException_Result result = {};
    limbo_handler.RetrieveException(infos[2].process_koid,
                                    [&called, &result](ProcessLimbo_RetrieveException_Result res) {
                                      called = true;
                                      result = std::move(res);
                                    });
    ASSERT_TRUE(called);
    ASSERT_TRUE(result.is_response());
    ValidateException(excps[2], result.response().process_exception);

    // There should be one less exception.
    ASSERT_EQ(limbo_manager.limbo().size(), 1u);
  }

  {
    // Getting the last one should work.
    bool called = false;
    ProcessLimbo_ReleaseProcess_Result release_result = {};
    limbo_handler.ReleaseProcess(
        infos[1].process_koid, [&called, &release_result](ProcessLimbo_ReleaseProcess_Result res) {
          called = true;
          release_result = std::move(res);
        });
    ASSERT_TRUE(called);
    ASSERT_TRUE(release_result.is_response());

    // There should be one less exception.
    ASSERT_EQ(limbo_manager.limbo().size(), 0u);
  }

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  excps[0].job.kill();
  excps[1].job.kill();
  excps[2].job.kill();
}

TEST(ProcessLimboManager, FromExceptionBroker) {
  auto test_context = CreateTestContext();
  auto broker = ExceptionBroker::Create(test_context->loop.dispatcher(),
                                        test_context->services.service_directory());
  ASSERT_TRUE(broker);
  broker->limbo_manager().set_active(true);

  // We create multiple exceptions.
  ExceptionContext excps[3];
  ASSERT_TRUE(RetrieveExceptionContext(excps + 0));
  ASSERT_TRUE(RetrieveExceptionContext(excps + 1));
  ASSERT_TRUE(RetrieveExceptionContext(excps + 2));

  // Get the fidl representation of the exception.
  ExceptionInfo infos[3];
  infos[0] = ExceptionContextToExceptionInfo(excps[0]);
  infos[1] = ExceptionContextToExceptionInfo(excps[1]);
  infos[2] = ExceptionContextToExceptionInfo(excps[2]);

  // It's not easy to pass array references to lambdas.
  bool cb_call0 = false;
  bool cb_call1 = false;
  bool cb_call2 = false;
  broker->OnException(std::move(excps[0].exception), infos[0], [&cb_call0]() { cb_call0 = true; });
  broker->OnException(std::move(excps[1].exception), infos[1], [&cb_call1]() { cb_call1 = true; });
  broker->OnException(std::move(excps[2].exception), infos[2], [&cb_call2]() { cb_call2 = true; });

  // There should not be an outgoing connection and no reports generated.
  ASSERT_TRUE(broker->connections().empty());

  // There should be 3 exceptions on the limbo.
  auto& limbo = broker->limbo_manager().limbo();
  ValidateException(excps[0], limbo.find(excps[0].process_koid)->second);
  ValidateException(excps[1], limbo.find(excps[1].process_koid)->second);
  ValidateException(excps[2], limbo.find(excps[2].process_koid)->second);

  // We kill the jobs. This kills the underlying process. We do this so that the crashed process
  // doesn't get rescheduled. Otherwise the exception on the crash program would bubble out of our
  // environment and create noise on the overall system.
  excps[0].job.kill();
  excps[1].job.kill();
  excps[2].job.kill();
}

}  // namespace
}  // namespace exception
}  // namespace fuchsia
