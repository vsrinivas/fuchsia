// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/lib/driver-integration-test/fixture.h"

#include <fidl/fuchsia.board.test/cpp/wire.h>
#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/syslog/global.h>

#include "sdk/lib/device-watcher/cpp/device-watcher.h"

namespace driver_integration_test {

using namespace sys::testing;

zx_status_t IsolatedDevmgr::Create(Args* args, IsolatedDevmgr* out) {
  IsolatedDevmgr devmgr;
  devmgr.loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);

  // Create and build the realm.
  auto realm_builder = sys::testing::Realm::Builder::Create();
  driver_test_realm::Setup(realm_builder);

  // Setup Fshost.
  if (args->disable_block_watcher) {
    realm_builder.AddComponent(
        Moniker{"fshost"}, Component{.source = ComponentUrl{"#meta/test-fshost-no-watcher.cm"}});

  } else {
    realm_builder.AddComponent(Moniker{"fshost"},
                               Component{.source = ComponentUrl{"#meta/test-fshost.cm"}});
  }
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.process.Launcher"},
                                         .source = {AboveRoot()},
                                         .targets = {Moniker{"fshost"}}});
  realm_builder.AddRoute(
      CapabilityRoute{.capability = Protocol{"fuchsia.device.manager.Administrator"},
                      .source = {Moniker{"driver_test_realm"}},
                      .targets = {Moniker{"fshost"}}});
  realm_builder.AddRoute(
      CapabilityRoute{.capability = Protocol{"fuchsia.hardware.power.statecontrol.Admin"},
                      .source = {Moniker{"driver_test_realm"}},
                      .targets = {Moniker{"fshost"}}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.logger.LogSink"},
                                         .source = {AboveRoot()},
                                         .targets = {Moniker{"fshost"}}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.fshost.BlockWatcher"},
                                         .source = {Moniker{"fshost"}},
                                         .targets = {AboveRoot()}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.fshost.Admin"},
                                         .source = {Moniker{"fshost"}},
                                         .targets = {AboveRoot()}});
  realm_builder.AddRoute(CapabilityRoute{.capability = Protocol{"fuchsia.fshost.Loader"},
                                         .source = {Moniker{"fshost"}},
                                         .targets = {AboveRoot()}});
  realm_builder.AddRoute(CapabilityRoute{
      .capability = Directory{"factory", "factory", fuchsia::io2::Operations::CONNECT},
      .source = {Moniker{"fshost"}},
      .targets = {AboveRoot()}});
  realm_builder.AddRoute(CapabilityRoute{
      .capability = Directory{"durable", "durable", fuchsia::io2::Operations::CONNECT},
      .source = {Moniker{"fshost"}},
      .targets = {AboveRoot()}});
  realm_builder.AddRoute(CapabilityRoute{
      .capability = Directory{"install", "install", fuchsia::io2::Operations::CONNECT},
      .source = {Moniker{"fshost"}},
      .targets = {AboveRoot()}});
  realm_builder.AddRoute(
      CapabilityRoute{.capability = Directory{"tmp", "tmp", fuchsia::io2::Operations::CONNECT},
                      .source = {Moniker{"fshost"}},
                      .targets = {AboveRoot()}});
  realm_builder.AddRoute(CapabilityRoute{
      .capability = Directory{"volume", "volume", fuchsia::io2::Operations::CONNECT},
      .source = {Moniker{"fshost"}},
      .targets = {AboveRoot()}});

  realm_builder.AddRoute(
      CapabilityRoute{.capability = Directory{"dev", "dev", fuchsia::io2::Operations::CONNECT},
                      .source = {Moniker{"driver_test_realm"}},
                      .targets = {Moniker{"fshost"}}});

  // Build the realm.
  devmgr.realm_ =
      std::make_unique<sys::testing::Realm>(realm_builder.Build(devmgr.loop_->dispatcher()));

  // Start DriverTestRealm.
  fidl::SynchronousInterfacePtr<fuchsia::driver::test::Realm> driver_test_realm;
  zx_status_t status = devmgr.realm_->Connect(driver_test_realm.NewRequest());
  if (status != ZX_OK) {
    return status;
  }

  fuchsia::driver::test::Realm_Start_Result realm_result;
  auto realm_args = fuchsia::driver::test::RealmArgs();
  realm_args.set_root_driver("fuchsia-boot:///#driver/platform-bus.so");
  realm_args.set_driver_log_level(args->log_level);
  realm_args.set_board_name(std::string(args->board_name.data()));
  status = driver_test_realm->Start(std::move(realm_args), &realm_result);
  if (status != ZX_OK) {
    return status;
  }
  if (realm_result.is_err()) {
    return realm_result.err();
  }

  // Connect to dev.
  fidl::InterfaceHandle<fuchsia::io::Directory> dev;
  status = devmgr.realm_->Connect("dev", dev.NewRequest().TakeChannel());
  if (status != ZX_OK) {
    return status;
  }

  status = fdio_fd_create(dev.TakeChannel().release(), devmgr.devfs_root_.reset_and_get_address());
  if (status != ZX_OK) {
    return status;
  }

  fbl::unique_fd platform_fd;
  status = device_watcher::RecursiveWaitForFile(devmgr.devfs_root_, "sys/platform/test-board",
                                                &platform_fd);
  if (status != ZX_OK) {
    return status;
  }

  zx::status client_end =
      fdio_cpp::FdioCaller(std::move(platform_fd)).take_as<fuchsia_board_test::Board>();
  if (client_end.status_value() != ZX_OK) {
    return client_end.status_value();
  }

  fidl::WireSyncClient client(std::move(*client_end));

  for (auto& device : args->device_list) {
    fuchsia_board_test::wire::Entry entry;
    entry.name = device.name;
    entry.vid = device.vid;
    entry.pid = device.pid;
    entry.did = device.did;
    std::vector<uint8_t> metadata(device.metadata, device.metadata + device.metadata_size);
    entry.metadata = fidl::VectorView<uint8_t>::FromExternal(metadata);
    auto status = client->CreateDevice(entry);
    if (status.status() != ZX_OK) {
      return status.status();
    }
  }

  *out = std::move(devmgr);
  return ZX_OK;
}

}  // namespace driver_integration_test
