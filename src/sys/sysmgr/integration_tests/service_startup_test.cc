// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/examples/echo/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fidl/cpp/string.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/service_directory.h>
#include <stdio.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <src/lib/files/glob.h>
#include <test/sysmgr/cpp/fidl.h>

#include "lib/sys/cpp/termination_reason.h"

namespace sysmgr {
namespace test {
namespace {

constexpr char kGlob[] = "/hub/r/sys/*/svc";

class SimpleLogCollector : public fuchsia::logger::LogListenerSafe {
 public:
  explicit SimpleLogCollector(fidl::InterfaceRequest<fuchsia::logger::LogListenerSafe> request,
                              async_dispatcher_t* dispatcher)
      : binding_(this, std::move(request), dispatcher) {
    binding_.set_error_handler([this](zx_status_t s) {
      if (!done_) {
        FAIL() << "Connection to simple collector closed early";
      }
    });
  }

  virtual void Log(fuchsia::logger::LogMessage message, LogCallback received) override {
    messages_.emplace_back(message.msg);
    received();
  };

  virtual void LogMany(std::vector<fuchsia::logger::LogMessage> messages,
                       LogManyCallback received) override {
    for (auto& l : messages) {
      Log(std::move(l), []() {});
    }
    received();
  }

  virtual void Done() override { done_ = true; }

  bool done_;
  fidl::Binding<fuchsia::logger::LogListenerSafe> binding_;
  std::vector<std::string> messages_;
};

class TestSysmgr : public ::gtest::RealLoopFixture {
 protected:
  // Verifies that messages with the given tags match |expected_patterns|.
  void VerifyLogs(const fuchsia::logger::LogPtr& log_ptr, std::vector<std::string> tags,
                  std::vector<std::string> expected_patterns) {
    fidl::InterfaceHandle<fuchsia::logger::LogListenerSafe> listener_handle;
    SimpleLogCollector collector(listener_handle.NewRequest(), dispatcher());
    auto filter_options = std::make_unique<fuchsia::logger::LogFilterOptions>();
    filter_options->tags = tags;

    // FIXME(45589) can't use DumpLogs without a fence
    log_ptr->ListenSafe(std::move(listener_handle), std::move(filter_options));
    RunLoopUntil([&collector, &expected_patterns] {
      return (collector.messages_.size() == expected_patterns.size());
    });

    ASSERT_EQ(expected_patterns.size(), collector.messages_.size());
    auto expected = expected_patterns.begin();
    auto observed = collector.messages_.begin();
    while (expected != expected_patterns.end() || observed != collector.messages_.end()) {
      ASSERT_THAT(*observed, ::testing::MatchesRegex(*expected));
      expected++;
      observed++;
    }
    ASSERT_EQ(expected, expected_patterns.end());
    ASSERT_EQ(observed, collector.messages_.end());
  }
};

TEST_F(TestSysmgr, ServiceStartup) {
  // wait for sysmgr to destroy existing environments.
  RunLoopUntil([] {
    files::Glob glob(kGlob);
    return glob.size() == 0;
  });

  auto environment_services = sys::ComponentContext::CreateAndServeOutgoingDirectory()->svc();
  fuchsia::sys::LaunchInfo launch_info{
      .url = "fuchsia-pkg://fuchsia.com/sysmgr-integration-tests#meta/sysmgr.cmx"};
  fuchsia::sys::ServiceListPtr additional_services = std::make_unique<fuchsia::sys::ServiceList>();
  fuchsia::sys::LauncherPtr launcher;
  environment_services->Connect(launcher.NewRequest());

  fuchsia::sys::ComponentControllerPtr contoller;
  launcher->CreateComponent(std::move(launch_info), contoller.NewRequest());

  bool sysmgr_alive = true;
  contoller.events().OnTerminated = [&](int64_t return_code,
                                        fuchsia::sys::TerminationReason termination_reason) {
    fprintf(stderr, "sysmgr died: %s\n",
            sys::HumanReadableTerminationReason(termination_reason).c_str());
    sysmgr_alive = false;
  };

  // wait for sysmgr to create environment.
  std::string path;
  RunLoopUntil([&] {
    if (!sysmgr_alive) {
      return true;  // end loop if sysmgr died.
    }
    files::Glob glob(kGlob);
    if (glob.size() == 1u) {
      path = std::string(*glob.begin());
      return true;
    }
    return false;
  });

  ASSERT_TRUE(sysmgr_alive);
  // connect to nested environment's svc.
  zx::channel directory;
  auto sysmgr_svc = sys::ServiceDirectory::CreateWithRequest(&directory);

  ASSERT_EQ(ZX_OK, fdio_open(path.c_str(),
                             fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_WRITABLE,
                             directory.release()));

  std::string response;
  ::test::sysmgr::InterfaceSyncPtr interface_ptr;
  ASSERT_EQ(ZX_OK, sysmgr_svc->Connect(interface_ptr.NewRequest()));
  ASSERT_EQ(ZX_OK, interface_ptr->Ping(&response));

  EXPECT_EQ("test_sysmgr_service_startup", response);

  {
    // sysmgr should create the environment with parent services inherited.
    fidl::StringPtr echo_msg = "test string for echo";
    fidl::examples::echo::EchoSyncPtr echo_ptr;
    ASSERT_EQ(ZX_OK, sysmgr_svc->Connect(echo_ptr.NewRequest()));

    fidl::StringPtr response;
    ASSERT_EQ(ZX_OK, echo_ptr->EchoString(echo_msg, &response));
    EXPECT_EQ(echo_msg, response);
  }
}

}  // namespace
}  // namespace test
}  // namespace sysmgr
