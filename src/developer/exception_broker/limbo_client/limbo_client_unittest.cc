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

#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

namespace fuchsia {
namespace exception {
namespace {

class StubProcessLimbo : public ProcessLimbo {
 public:
  void set_active(bool active) { active_ = active; }

  // ProcessLimbo implementation.

  void WatchActive(WatchActiveCallback callback) override { callback(active_); }

  void WatchProcessesWaitingOnException(
      ProcessLimbo::WatchProcessesWaitingOnExceptionCallback callback) override {
    FXL_NOTREACHED() << "Not needed for tests.";
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
  std::vector<std::string> filters_;

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
  sys::testing::ServiceDirectoryProvider services;
  StubProcessLimbo process_limbo;
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

}  // namespace
}  // namespace exception
}  // namespace fuchsia
