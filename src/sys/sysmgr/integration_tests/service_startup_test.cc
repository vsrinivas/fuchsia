// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/logger/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>

#include <regex>

#include <fidl/examples/echo/cpp/fidl.h>
#include <gmock/gmock.h>
#include <test/sysmgr/cpp/fidl.h>

#include "gtest/gtest.h"
#include "src/sys/appmgr/appmgr.h"

namespace sysmgr {
namespace test {
namespace {

using TestSysmgr = ::gtest::RealLoopFixture;

class SimpleLogCollector : public fuchsia::logger::LogListener {
 public:
  explicit SimpleLogCollector(fidl::InterfaceRequest<fuchsia::logger::LogListener> request,
                              async_dispatcher_t* dispatcher)
      : binding_(this, std::move(request), dispatcher) {
    binding_.set_error_handler([this](zx_status_t s) {
      if (!done_) {
        FAIL() << "Connection to simple collector closed early";
      }
    });
  }

  virtual void Log(fuchsia::logger::LogMessage message) override {
    messages_.emplace_back(message.msg);
  };

  virtual void LogMany(std::vector<fuchsia::logger::LogMessage> messages) override {
    for (auto& l : messages) {
      Log(std::move(l));
    }
  }

  virtual void Done() override { done_ = true; }

  bool done_;
  fidl::Binding<fuchsia::logger::LogListener> binding_;
  std::vector<std::string> messages_;
};

TEST_F(TestSysmgr, ServiceStartup) {
  zx::channel h1, h2;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &h1, &h2));

  auto environment_services = sys::ComponentContext::Create()->svc();

  // Make fidl.examples.echo.Echo from our own environment available in appmgr's
  // root realm.
  fuchsia::sys::ServiceListPtr root_realm_services(new fuchsia::sys::ServiceList);
  root_realm_services->names = std::vector<std::string>{fidl::examples::echo::Echo::Name_};
  root_realm_services->host_directory = environment_services->CloneChannel().TakeChannel();

  std::vector<std::string> sysmgr_args;
  component::AppmgrArgs args{
      .pa_directory_request = h2.release(),
      .root_realm_services = std::move(root_realm_services),
      .environment_services = std::move(environment_services),
      .sysmgr_url = "fuchsia-pkg://fuchsia.com/sysmgr_integration_tests#meta/sysmgr.cmx",
      .sysmgr_args = std::move(sysmgr_args),
      .run_virtual_console = false,
      .retry_sysmgr_crash = false};
  component::Appmgr appmgr(dispatcher(), std::move(args));

  // h1 is connected to h2, which is injected above as appmgr's
  // PA_DIRECTORY_REQUEST handle. appmgr hosts a directory on that handle, which
  // includes a svc/ subdirectory, which in turn connects to the first realm's
  // services. That first realm is the sys realm created by sysmgr, so
  // sysmgr_svc ends up being a directory with all services in the sys realm.
  zx::channel svc_client, svc_server;
  ASSERT_EQ(ZX_OK, zx::channel::create(0, &svc_client, &svc_server));
  ASSERT_EQ(ZX_OK, fdio_service_connect_at(h1.get(), "svc", svc_server.release()));
  sys::ServiceDirectory sysmgr_svc(std::move(svc_client));

  bool received_response = false;
  std::string response;
  ::test::sysmgr::InterfacePtr interface_ptr;
  ASSERT_EQ(ZX_OK, sysmgr_svc.Connect(interface_ptr.NewRequest(dispatcher())));

  fuchsia::logger::LogPtr log_ptr;
  fidl::InterfaceHandle<fuchsia::logger::LogListener> listener_handle;
  SimpleLogCollector collector(listener_handle.NewRequest(), dispatcher());
  ASSERT_EQ(ZX_OK, sysmgr_svc.Connect(log_ptr.NewRequest(dispatcher())));

  interface_ptr->Ping([&](fidl::StringPtr r) {
    received_response = true;
    response = r.value_or("");
  });
  RunLoopUntil([&] { return received_response; });
  EXPECT_EQ("test_sysmgr_service_startup", response);

  const std::string echo_msg = "test string for echo";
  received_response = false;
  fidl::examples::echo::EchoPtr echo_ptr;
  ASSERT_EQ(ZX_OK, sysmgr_svc.Connect(echo_ptr.NewRequest(dispatcher())));

  echo_ptr->EchoString(echo_msg, [&](fidl::StringPtr r) {
    received_response = true;
    response = r.value_or("");
  });
  RunLoopUntil([&] { return received_response; });
  EXPECT_EQ(echo_msg, response);

  auto filter_options = fuchsia::logger::LogFilterOptions::New();
  std::vector<std::string> expected_patterns{
      "test_sysmgr_service.cc\\([0-9]{1,4}\\): Entering loop.",
      "test_sysmgr_service.cc\\([0-9]{1,4}\\): Received ping.",
  };
  // FIXME(45589) can't use DumpLogs without a fence
  log_ptr->Listen(std::move(listener_handle), std::move(filter_options));
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

}  // namespace
}  // namespace test
}  // namespace sysmgr
