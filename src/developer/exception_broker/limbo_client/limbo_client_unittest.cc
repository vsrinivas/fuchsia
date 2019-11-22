// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/exception_broker/limbo_client/limbo_client.h"

#include <fuchsia/exception/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/testing/service_directory_provider.h>

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

  void GetFilters(GetFiltersCallback) override { FXL_NOTREACHED() << "Not needed for tests."; }
  void AppendFilters(std::vector<std::string> filters, AppendFiltersCallback) override {
    FXL_NOTREACHED() << "Not needed for tests.";
  }
  void RemoveFilters(std::vector<std::string> filters, RemoveFiltersCallback) override {
    FXL_NOTREACHED() << "Not needed for tests.";
  }

  // Service Directory handling

  // Boilerplate needed for getting a FIDL binding to work in unit tests.
  fidl::InterfaceRequestHandler<ProcessLimbo> GetHandler() { return bindings_.GetHandler(this); }

 private:
  bool active_ = false;

  fidl::BindingSet<ProcessLimbo> bindings_;
};

TEST(LimboClient, Init) {
  StubProcessLimbo process_limbo;
  process_limbo.set_active(true);

  async::Loop remote_loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  sys::testing::ServiceDirectoryProvider services(remote_loop.dispatcher());
  services.AddService(process_limbo.GetHandler());
  ASSERT_EQ(remote_loop.StartThread("process-limbo-thread"), ZX_OK);

  async::Loop local_loop(&kAsyncLoopConfigAttachToCurrentThread);

  LimboClient client(services.service_directory());
  ASSERT_FALSE(client.active());

  ASSERT_EQ(client.Init(), ZX_OK);
  EXPECT_TRUE(client.active());
}

}  // namespace
}  // namespace exception
}  // namespace fuchsia
