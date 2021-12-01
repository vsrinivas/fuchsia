// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "include/lib/devmgr-integration-test/fixture.h"

#include <fidl/fuchsia.driver.framework/cpp/wire.h>
#include <fuchsia/driver/test/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/driver_test_realm/realm_builder/cpp/lib.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl-async/bind.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/vfs/cpp/remote_dir.h>
#include <stdint.h>
#include <zircon/status.h>

#include <fbl/ref_ptr.h>

namespace devmgr_integration_test {

namespace {
constexpr std::string_view kBootPath = "/boot/";
constexpr std::string_view kBootUrlPrefix = "fuchsia-boot:///#";

std::string PathToUrl(std::string_view path) {
  if (path.find(kBootUrlPrefix) == 0) {
    return std::string(path);
  }
  if (path.find(kBootPath) != 0) {
    ZX_ASSERT_MSG(false, "Driver path to devmgr-launcher must start with /boot/!");
  }
  return std::string(kBootUrlPrefix).append(path.substr(kBootPath.size()));
}
}  // namespace

__EXPORT
zx_status_t IsolatedDevmgr::AddDevfsToOutgoingDir(vfs::PseudoDir* outgoing_root_dir) {
  zx::channel client, server;
  auto status = zx::channel::create(0, &client, &server);
  if (status != ZX_OK) {
    return status;
  }
  fdio_cpp::UnownedFdioCaller fd(devfs_root_.get());
  fdio_service_clone_to(fd.borrow_channel(), server.release());

  // Add devfs to out directory.
  auto devfs_out = std::make_unique<vfs::RemoteDir>(std::move(client));
  outgoing_root_dir->AddEntry("dev", std::move(devfs_out));
  return ZX_OK;
}

__EXPORT
devmgr_launcher::Args IsolatedDevmgr::DefaultArgs() {
  devmgr_launcher::Args args;
  args.sys_device_driver = "/boot/driver/sysdev.so";
  return args;
}

__EXPORT
IsolatedDevmgr::IsolatedDevmgr() = default;

__EXPORT
zx_status_t IsolatedDevmgr::Create(devmgr_launcher::Args args, IsolatedDevmgr* out) {
  return Create(std::move(args), nullptr, out);
}

__EXPORT
zx_status_t IsolatedDevmgr::Create(devmgr_launcher::Args args, async_dispatcher_t* dispatcher,
                                   IsolatedDevmgr* out) {
  IsolatedDevmgr devmgr;
  devmgr.loop_ = std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToCurrentThread);

  // Create and build the realm.
  auto realm_builder = sys::testing::Realm::Builder::Create();
  driver_test_realm::Setup(realm_builder);
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
  realm_args.set_root_driver(PathToUrl(args.sys_device_driver));
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

  *out = std::move(devmgr);
  return ZX_OK;
}

IsolatedDevmgr::IsolatedDevmgr(IsolatedDevmgr&& other)
    : loop_(std::move(other.loop_)),
      realm_(std::move(other.realm_)),
      devfs_root_(std::move(other.devfs_root_)) {}

__EXPORT
IsolatedDevmgr& IsolatedDevmgr::operator=(IsolatedDevmgr&& other) {
  loop_ = std::move(other.loop_);
  realm_ = std::move(other.realm_);
  devfs_root_ = std::move(other.devfs_root_);
  return *this;
}

__EXPORT
IsolatedDevmgr::~IsolatedDevmgr() = default;

}  // namespace devmgr_integration_test
