// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "integration_test_lib.h"

#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/net/virtualization/cpp/fidl.h>
#include <fuchsia/netemul/guest/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/scheduler/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/testing/predicates/status.h"
#include "src/virtualization/tests/lib/guest_console.h"

static fit::closure MakeRecurringTask(async_dispatcher_t* dispatcher, fit::closure cb,
                                      zx::duration frequency) {
  return [dispatcher, cb = std::move(cb), frequency]() mutable {
    cb();
    async::PostDelayedTask(dispatcher, MakeRecurringTask(dispatcher, std::move(cb), frequency),
                           frequency);
  };
}

void GuestInteractionTest::GetHostVsockEndpoint(
    ::fidl::InterfaceRequest<::fuchsia::virtualization::HostVsockEndpoint> endpoint) {
  std::optional<fuchsia::virtualization::Guest_GetHostVsockEndpoint_Result> vsock_result;
  guest_->GetHostVsockEndpoint(
      std::move(endpoint),
      [&vsock_result](fuchsia::virtualization::Guest_GetHostVsockEndpoint_Result result) {
        vsock_result = std::move(result);
      });

  bool loop_result =
      RunLoopWithTimeoutOrUntil([&vsock_result] { return vsock_result.has_value(); }, zx::sec(5));
  FX_CHECK(loop_result && vsock_result->is_response());
}

void GuestInteractionTest::SetUp() {
  using component_testing::ChildRef;
  using component_testing::Directory;
  using component_testing::ParentRef;
  using component_testing::Protocol;
  using component_testing::RealmBuilder;
  using component_testing::RealmRoot;
  using component_testing::Route;

  // Launch the Debian guest
  constexpr auto kFakeNetstackComponentName = "fake_netstack";
  constexpr auto kDebianGuestManagerUrl = "#meta/debian_guest_manager.cm";

  constexpr auto kGuestManagerName = "guest_manager";

  fuchsia::virtualization::GuestConfig cfg;
  cfg.set_virtio_gpu(false);

  auto realm_builder = RealmBuilder::Create();
  realm_builder.AddChild(kGuestManagerName, kDebianGuestManagerUrl);
  realm_builder.AddLocalChild(kFakeNetstackComponentName, &fake_netstack_);

  realm_builder
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::logger::LogSink::Name_},
                              Protocol{fuchsia::kernel::HypervisorResource::Name_},
                              Protocol{fuchsia::kernel::VmexResource::Name_},
                              Protocol{fuchsia::sysinfo::SysInfo::Name_},
                              Protocol{fuchsia::sysmem::Allocator::Name_},
                              Protocol{fuchsia::tracing::provider::Registry::Name_},
                              Protocol{fuchsia::scheduler::ProfileProvider::Name_},
                          },
                      .source = {ParentRef()},
                      .targets = {ChildRef{kGuestManagerName}}})
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::net::virtualization::Control::Name_},
                          },
                      .source = {ChildRef{kFakeNetstackComponentName}},
                      .targets = {ChildRef{kGuestManagerName}}})
      .AddRoute(Route{.capabilities =
                          {
                              Protocol{fuchsia::virtualization::DebianGuestManager::Name_},
                          },
                      .source = ChildRef{kGuestManagerName},
                      .targets = {ParentRef()}});

  realm_root_ = std::make_unique<RealmRoot>(realm_builder.Build(dispatcher()));
  fuchsia::virtualization::GuestManager_LaunchGuest_Result res;
  guest_manager_ = realm_root_->ConnectSync<fuchsia::virtualization::DebianGuestManager>();

  FX_LOGS(INFO) << "Starting Debian Guest";
  ASSERT_OK(guest_manager_->LaunchGuest(std::move(cfg), guest_.NewRequest(), &res));
  if (res.is_err()) {
    FAIL() << "Failed to launch guest with error: " << static_cast<uint32_t>(res.err());
  }

  // Start a GuestConsole.  When the console starts, it waits until it
  // receives some sensible output from the guest to ensure that the guest is
  // usable.
  FX_LOGS(INFO) << "Getting Serial Console";
  std::optional<zx_status_t> guest_error;
  guest_.set_error_handler([&guest_error](zx_status_t status) { guest_error = status; });
  std::optional<fuchsia::virtualization::Guest_GetConsole_Result> get_console_result;
  guest_->GetConsole(
      [&get_console_result](fuchsia::virtualization::Guest_GetConsole_Result result) {
        get_console_result = std::move(result);
      });
  FX_LOGS(INFO) << "Waiting for Serial Console";
  RunLoopUntil([&guest_error, &get_console_result]() {
    return guest_error.has_value() || get_console_result.has_value();
  });
  FX_LOGS(INFO) << "Serial Console Received";
  ASSERT_FALSE(guest_error.has_value()) << zx_status_get_string(guest_error.value());
  fuchsia::virtualization::Guest_GetConsole_Result& result = get_console_result.value();
  switch (result.Which()) {
    case fuchsia::virtualization::Guest_GetConsole_Result::Tag::kResponse: {
      GuestConsole serial(std::make_unique<ZxSocket>(std::move(result.response().socket)));
      ASSERT_OK(serial.Start(zx::time::infinite()));

      // Make sure the pty is running and that the guest will receive our commands.
      ASSERT_OK(serial.RepeatCommandTillSuccess("echo guest ready", "$", "guest ready",
                                                zx::time::infinite(), zx::sec(1)));

      // Wait until guest_interaction_daemon is running.
      ASSERT_OK(serial.ExecuteBlocking(
          "journalctl -f --no-tail -u guest_interaction_daemon | grep -m1 Listening", "$",
          zx::time::infinite(), nullptr));

      // Periodically log the guest state.
      MakeRecurringTask(
          dispatcher(),
          [serial = std::move(serial), log_count = 0]() mutable {
            ASSERT_OK(
                serial.ExecuteBlocking("echo " + std::to_string(++log_count) +
                                           "; journalctl -u guest_interaction_daemon --no-pager",
                                       "$", zx::time::infinite(), nullptr));
          },
          zx::sec(10))();

      break;
    }
    case fuchsia::virtualization::Guest_GetConsole_Result::Tag::kErr:
      FAIL() << "fuchsia.virtualization/Guest.GetConsole error: "
             << static_cast<int32_t>(result.err());
    case fuchsia::virtualization::Guest_GetConsole_Result::Tag::Invalid:
      FAIL() << "fuchsia.virtualization/Guest.GetConsole: invalid FIDL tag";
  }
}
