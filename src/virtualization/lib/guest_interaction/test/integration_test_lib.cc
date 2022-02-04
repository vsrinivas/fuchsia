// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "integration_test_lib.h"

#include <fuchsia/kernel/cpp/fidl.h>
#include <fuchsia/net/stack/cpp/fidl.h>
#include <fuchsia/netemul/guest/cpp/fidl.h>
#include <fuchsia/netstack/cpp/fidl.h>
#include <fuchsia/sysinfo/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/file_descriptor.h>

#include <src/virtualization/tests/guest_console.h>

#include "src/lib/testing/predicates/status.h"

static constexpr char kGuestManagerUrl[] =
    "fuchsia-pkg://fuchsia.com/guest_manager#meta/guest_manager.cmx";
static constexpr char kDebianGuestUrl[] =
    "fuchsia-pkg://fuchsia.com/debian_guest#meta/debian_guest.cmx";

GuestInteractionTest::GuestInteractionTest() {
  realm_.set_error_handler([this](zx_status_t status) { realm_error_ = status; });

  // Add Netstack services.
  fake_netstack_.Install(*services_);

  // Add guest service.
  services_->AddServiceWithLaunchInfo(
      {
          .url = kGuestManagerUrl,
          .out = sys::CloneFileDescriptor(STDOUT_FILENO),
          .err = sys::CloneFileDescriptor(STDERR_FILENO),
      },
      fuchsia::virtualization::Manager::Name_);

  // Allow services required for virtualization.
  services_->AllowParentService(fuchsia::kernel::HypervisorResource::Name_);
  services_->AllowParentService(fuchsia::kernel::VmexResource::Name_);
  services_->AllowParentService(fuchsia::sysinfo::SysInfo::Name_);
}

static fit::closure MakeRecurringTask(async_dispatcher_t* dispatcher, fit::closure cb,
                                      zx::duration frequency) {
  return [dispatcher, cb = std::move(cb), frequency]() mutable {
    cb();
    async::PostDelayedTask(dispatcher, MakeRecurringTask(dispatcher, std::move(cb), frequency),
                           frequency);
  };
}

void GuestInteractionTest::SetUp() {
  ASSERT_NE(services_, nullptr);
  ASSERT_EQ(env_, nullptr);

  env_ = CreateNewEnclosingEnvironment("GuestInteractionEnvironment", std::move(services_));

  // Launch the Debian guest
  fuchsia::virtualization::GuestConfig cfg;
  cfg.set_virtio_gpu(false);

  fuchsia::virtualization::ManagerPtr manager;
  fuchsia::virtualization::GuestPtr guest;

  env_->ConnectToService(manager.NewRequest());
  manager->Create(fuchsia::netemul::guest::DEFAULT_REALM, realm_.NewRequest());
  realm_->LaunchInstance(kDebianGuestUrl, kGuestLabel, std::move(cfg), guest.NewRequest(),
                         [this](uint32_t cid) { cid_ = cid; });
  RunLoopUntil([this]() { return realm_error_.has_value() || cid_.has_value(); });
  ASSERT_FALSE(realm_error_.has_value()) << zx_status_get_string(realm_error_.value());

  // Start a GuestConsole.  When the console starts, it waits until it
  // receives some sensible output from the guest to ensure that the guest is
  // usable.
  std::optional<zx_status_t> guest_error;
  guest.set_error_handler([&guest_error](zx_status_t status) { guest_error = status; });
  std::optional<fuchsia::virtualization::Guest_GetConsole_Result> get_console_result;
  guest->GetConsole([&get_console_result](fuchsia::virtualization::Guest_GetConsole_Result result) {
    get_console_result = std::move(result);
  });
  RunLoopUntil([&guest_error, &get_console_result]() {
    return guest_error.has_value() || get_console_result.has_value();
  });
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
          [serial = std::move(serial)]() mutable {
            ASSERT_OK(serial.ExecuteBlocking("journalctl -u guest_interaction_daemon --no-pager",
                                             "$", zx::time::infinite(), nullptr));
          },
          zx::sec(10))();

      break;
    }
    case fuchsia::virtualization::Guest_GetConsole_Result::Tag::kErr:
      FAIL() << zx_status_get_string(result.err());
    case fuchsia::virtualization::Guest_GetConsole_Result::Tag::Invalid:
      FAIL() << "fuchsia.virtualization/Guest.GetConsole: invalid FIDL tag";
  }
}
