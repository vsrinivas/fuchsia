// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/debug/debug_agent/limbo_provider.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <zircon/status.h>

#include <thread>

#include <gtest/gtest.h>

#include "src/developer/debug/debug_agent/mock_object_provider.h"
#include "src/lib/fxl/logging.h"

using namespace ::fuchsia::exception;

namespace debug_agent {
namespace {

// Stubs -------------------------------------------------------------------------------------------

class StubProcessLimbo : public fuchsia::exception::ProcessLimbo {
 public:
  void ListProcessesWaitingOnException(
      ProcessLimbo::ListProcessesWaitingOnExceptionCallback callback) override {
    callback(std::move(processes_));
  }

  void RetrieveException(uint64_t process_koid,
                         ProcessLimbo::RetrieveExceptionCallback callback) override {
    // TODO(donosoc): Implement retrieving exception.
    FXL_NOTIMPLEMENTED();
    FXL_NOTREACHED();
  }

  void AppendException(const MockProcessObject& process, const MockThreadObject& thread,
                       ExceptionType exception_type) {
    ExceptionInfo info = {};
    info.process_koid = process.koid;
    info.thread_koid = thread.koid;
    info.type = exception_type;

    ProcessExceptionMetadata metadata = {};
    metadata.set_info(std::move(info));
    // Sadly we cannot send bad handles over a channel, so we cannot actually send the "invented"
    // handles for this test. Setting the info is enough though.
    // metadata.set_process(process.GetHandle());
    // metadata.set_thread(thread.GetHandle());

    processes_.push_back(std::move(metadata));
  }

  // Boilerplate needed for getting a FIDL binding to work in unit tests.
  fidl::InterfaceRequestHandler<ProcessLimbo> GetHandler() { return bindings_.GetHandler(this); }

 private:
  std::vector<ProcessExceptionMetadata> processes_;
  fidl::BindingSet<ProcessLimbo> bindings_;
};

std::pair<const MockProcessObject*, const MockThreadObject*> GetProcessThread(
    const MockObjectProvider& object_provider, const std::string& process_name,
    const std::string& thread_name) {
  const MockProcessObject* process = object_provider.ProcessByName(process_name);
  FXL_DCHECK(process);
  const MockThreadObject* thread = process->GetThread(thread_name);
  FXL_DCHECK(thread);

  return {process, thread};
}

// Tests -------------------------------------------------------------------------------------------

TEST(LimboProvider, ListProcessesOnException) {
  // Set the process limbo.
  auto object_provider = CreateDefaultMockObjectProvider();
  auto [process1, thread1] = GetProcessThread(*object_provider, "root-p2", "initial-thread");

  constexpr ExceptionType exception1 = ExceptionType::FATAL_PAGE_FAULT;
  auto [process2, thread2] = GetProcessThread(*object_provider, "job121-p2", "third-thread");
  constexpr ExceptionType exception2 = ExceptionType::UNALIGNED_ACCESS;

  StubProcessLimbo process_limbo;
  process_limbo.AppendException(*process1, *thread1, exception1);
  process_limbo.AppendException(*process2, *thread2, exception2);

  // Setup the async loop to respond to the async call.
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  sys::testing::ServiceDirectoryProvider services(loop.dispatcher());
  services.AddService(process_limbo.GetHandler());
  ASSERT_EQ(loop.StartThread("process-limbo-thread"), ZX_OK);

  LimboProvider limbo_provider(services.service_directory());

  std::vector<ProcessExceptionMetadata> processes;
  zx_status_t status = limbo_provider.ListProcessesOnLimbo(&processes);
  ASSERT_EQ(status, ZX_OK) << zx_status_get_string(status);

  ASSERT_EQ(processes.size(), 2u);
  ASSERT_TRUE(processes[0].has_info());
  EXPECT_EQ(processes[0].info().process_koid, process1->koid);
  EXPECT_EQ(processes[0].info().thread_koid, thread1->koid);
  EXPECT_EQ(processes[0].info().type, exception1);
  ASSERT_TRUE(processes[1].has_info());
  EXPECT_EQ(processes[1].info().process_koid, process2->koid);
  EXPECT_EQ(processes[1].info().thread_koid, thread2->koid);
  EXPECT_EQ(processes[1].info().type, exception2);
}

}  // namespace
}  // namespace debug_agent
