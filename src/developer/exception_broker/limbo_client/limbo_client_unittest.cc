// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/exception_broker/limbo_client/limbo_client.h"

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include <gtest/gtest.h>

#include "src/developer/exception_broker/limbo_client/options.h"
#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace exception {
namespace {

class StubProcessLimbo : public ProcessLimbo {
 public:
  void set_active(bool active) { active_ = active; }
  bool active_call() const { return active_call_; }

  bool has_active_call() const { return has_active_call_; }

  void AppendException(zx_koid_t process_koid, zx_koid_t thread_koid, zx_excp_type_t exception) {
    exceptions_.push_back({process_koid, thread_koid, exception});
  }

  // ProcessLimbo implementation.

  void SetActive(bool active, SetActiveCallback cb) override {
    has_active_call_ = true;
    active_call_ = active;
    cb();
  }

  void WatchActive(WatchActiveCallback callback) override { callback(active_); }

  void WatchProcessesWaitingOnException(
      ProcessLimbo::WatchProcessesWaitingOnExceptionCallback callback) override {
    std::vector<ProcessExceptionMetadata> exceptions;
    exceptions.reserve(exceptions_.size());
    for (auto [process_koid, thread_koid, exception] : exceptions_) {
      ExceptionInfo info = {};
      info.process_koid = process_koid;
      info.thread_koid = thread_koid;
      info.type = static_cast<ExceptionType>(exception);

      ProcessExceptionMetadata metadata = {};
      metadata.set_info(std::move(info));

      zx::process process;
      FXL_CHECK(zx::process::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &process) == ZX_OK);
      metadata.set_process(std::move(process));

      zx::thread thread;
      FXL_CHECK(zx::thread::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &thread) == ZX_OK);
      metadata.set_thread(std::move(thread));

      exceptions.push_back(std::move(metadata));
    }

    callback(fit::ok(std::move(exceptions)));
  }

  void RetrieveException(zx_koid_t process_koid,
                         ProcessLimbo::RetrieveExceptionCallback callback) override {
    FXL_NOTREACHED() << "Not needed for tests.";
  }

  void ReleaseProcess(zx_koid_t process_koid, ProcessLimbo::ReleaseProcessCallback cb) override {
    FXL_NOTREACHED() << "Not needed for tests.";
  }

  void GetFilters(GetFiltersCallback callback) override { callback(filters_); }

  void AppendFilters(std::vector<std::string> filters, AppendFiltersCallback callback) override {
    filters_ = std::move(filters);
    callback(fit::ok());
  }

  void RemoveFilters(std::vector<std::string> filters, RemoveFiltersCallback) override {
    FXL_NOTREACHED() << "Not needed for tests.";
  }

  // Service Directory handling

  // Boilerplate needed for getting a FIDL binding to work in unit tests.
  fidl::InterfaceRequestHandler<ProcessLimbo> GetHandler() { return bindings_.GetHandler(this); }

 private:
  bool active_ = false;
  bool active_call_ = false;
  bool has_active_call_ = false;

  std::vector<std::string> filters_;
  std::vector<std::tuple<zx_koid_t, zx_koid_t, zx_excp_type_t>> exceptions_;

  fidl::BindingSet<ProcessLimbo> bindings_;
};

struct TestContext {
  TestContext()
      : remote_loop(&kAsyncLoopConfigNoAttachToCurrentThread),
        local_loop(&kAsyncLoopConfigAttachToCurrentThread),
        services(remote_loop.dispatcher()) {
    process_limbo.set_active(true);

    services.AddService(process_limbo.GetHandler());
    if (remote_loop.StartThread("process-limbo-thread") != ZX_OK)
      assert(false);
  }

  ~TestContext() {
    remote_loop.Shutdown();
    local_loop.Shutdown();
  }

  async::Loop remote_loop;
  async::Loop local_loop;

  StubProcessLimbo process_limbo;
  sys::testing::ServiceDirectoryProvider services;
};

#define ASSERT_ZX_EQ(stmt, expected)                                                          \
  {                                                                                           \
    zx_status_t status = (stmt);                                                              \
    ASSERT_EQ(status, expected) << "Expected " << zx_status_get_string(expected) << std::endl \
                                << "Got: " << zx_status_get_string(status);                   \
  }

// Tests -------------------------------------------------------------------------------------------

TEST(LimboClient, Init) {
  TestContext context;

  LimboClient client(context.services.service_directory());
  ASSERT_FALSE(client.active());

  ASSERT_ZX_EQ(client.Init(), ZX_OK);
  EXPECT_TRUE(client.active());
}

TEST(LimboClient, Filters) {
  TestContext context;

  LimboClient client(context.services.service_directory());
  ASSERT_ZX_EQ(client.Init(), ZX_OK);

  // First filters should be empty.
  {
    std::vector<std::string> filters;
    ASSERT_ZX_EQ(client.GetFilters(&filters), ZX_OK);
    EXPECT_EQ(filters.size(), 0u);
  }

  // Setting some filters should return a different amount.
  {
    ASSERT_ZX_EQ(client.AppendFilters({"filter-1", "filter-2"}), ZX_OK);

    std::vector<std::string> filters;
    ASSERT_ZX_EQ(client.GetFilters(&filters), ZX_OK);

    ASSERT_EQ(filters.size(), 2u);
    EXPECT_EQ(filters[0], "filter-1");
    EXPECT_EQ(filters[1], "filter-2");
  }
}

TEST(LimboClient, ListProcesses) {
  TestContext context;

  LimboClient client(context.services.service_directory());
  ASSERT_ZX_EQ(client.Init(), ZX_OK);

  constexpr zx_koid_t kProcessKoid1 = 0x1;
  constexpr zx_koid_t kProcessKoid2 = 0x2;
  constexpr zx_koid_t kThreadKoid1 = 0x3;
  constexpr zx_koid_t kThreadKoid2 = 0x4;

  constexpr zx_excp_type_t kException1 = ZX_EXCP_UNALIGNED_ACCESS;
  constexpr zx_excp_type_t kException2 = ZX_EXCP_SW_BREAKPOINT;

  context.process_limbo.AppendException(kProcessKoid1, kThreadKoid1, kException1);
  context.process_limbo.AppendException(kProcessKoid2, kThreadKoid2, kException2);

  {
    std::vector<LimboClient::ProcessDescription> processes;
    ASSERT_ZX_EQ(client.ListProcesses(&processes), ZX_OK);

    ASSERT_EQ(processes.size(), 2u);
    EXPECT_EQ(processes[0].process_koid, kProcessKoid1);
    EXPECT_EQ(processes[0].thread_koid, kThreadKoid1);
    EXPECT_EQ(processes[0].thread_name, "process-limbo-thread");
    EXPECT_EQ(processes[0].exception, kException1);

    EXPECT_EQ(processes[1].process_koid, kProcessKoid2);
    EXPECT_EQ(processes[1].thread_koid, kThreadKoid2);
    EXPECT_EQ(processes[1].thread_name, "process-limbo-thread");
    EXPECT_EQ(processes[1].exception, kException2);
  }
}

TEST(LimboClient, InvalidOption) {
  std::stringstream ss;
  ASSERT_EQ(ParseArgs(1, nullptr, ss), nullptr);

  const char* kArgs[] = {"limbo.cmx", "<invalid>"};
  ASSERT_EQ(ParseArgs(2, kArgs, ss), nullptr);
}

TEST(LimboClient, Enable) {
  TestContext context;
  context.process_limbo.set_active(false);

  LimboClient client(context.services.service_directory());
  ASSERT_ZX_EQ(client.Init(), ZX_OK);

  std::stringstream ss;

  const char* kArgs[] = {"limbo.cmx", "enable"};
  OptionFunction function = ParseArgs(2, kArgs, ss);
  ASSERT_TRUE(function);

  ASSERT_ZX_EQ(function(&client, ss), ZX_OK);
  ASSERT_TRUE(context.process_limbo.has_active_call());
  EXPECT_TRUE(context.process_limbo.active_call());
}

TEST(LimboClient, Disable) {
  TestContext context;
  context.process_limbo.set_active(true);

  LimboClient client(context.services.service_directory());
  ASSERT_ZX_EQ(client.Init(), ZX_OK);

  std::stringstream ss;

  const char* kArgs[] = {"limbo.cmx", "disable"};
  OptionFunction function = ParseArgs(2, kArgs, ss);
  ASSERT_TRUE(function);

  ASSERT_ZX_EQ(function(&client, ss), ZX_OK);
  ASSERT_TRUE(context.process_limbo.has_active_call());
  EXPECT_FALSE(context.process_limbo.active_call());
}

TEST(LimboClient, ListOption) {
  TestContext context;

  LimboClient client(context.services.service_directory());
  ASSERT_ZX_EQ(client.Init(), ZX_OK);

  constexpr zx_koid_t kProcessKoid1 = 1000;
  constexpr zx_koid_t kThreadKoid1 = 1001;
  constexpr zx_koid_t kProcessKoid2 = 2000;
  constexpr zx_koid_t kThreadKoid2 = 2001;

  constexpr zx_excp_type_t kException1 = ZX_EXCP_UNALIGNED_ACCESS;
  constexpr zx_excp_type_t kException2 = ZX_EXCP_SW_BREAKPOINT;

  context.process_limbo.AppendException(kProcessKoid1, kThreadKoid1, kException1);
  context.process_limbo.AppendException(kProcessKoid2, kThreadKoid2, kException2);

  {
    std::stringstream ss;
    const char* kArgs[] = {"limbo.cmx", "list"};

    OptionFunction function = ParseArgs(2, kArgs, ss);
    ASSERT_TRUE(function);

    ASSERT_ZX_EQ(function(&client, ss), ZX_OK);

    // The koids should be there.
    std::string msg = ss.str();
    EXPECT_NE(msg.find("1000"), std::string::npos);                      // kProcessKoid1
    EXPECT_NE(msg.find("1001"), std::string::npos);                      // kThreadKoid1
    EXPECT_NE(msg.find("ZX_EXCP_UNALIGNED_ACCESS"), std::string::npos);  // kException1.

    EXPECT_NE(msg.find("2000"), std::string::npos);                   // kProcessKoid2
    EXPECT_NE(msg.find("2001"), std::string::npos);                   // kThreadKoid2
    EXPECT_NE(msg.find("ZX_EXCP_SW_BREAKPOINT"), std::string::npos);  // kException1.
  }
}

}  // namespace
}  // namespace exception
}  // namespace fuchsia
