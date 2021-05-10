// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_PACKAGE_RESOLVER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_PACKAGE_RESOLVER_H_

#include <fuchsia/io/llcpp/fidl.h>
#include <fuchsia/pkg/llcpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include "src/devices/bin/driver_manager/driver.h"

namespace internal {

class PackageResolverInterface {
 public:
  virtual ~PackageResolverInterface() = default;

  virtual zx::status<std::unique_ptr<Driver>> FetchDriver(const std::string& package_url) = 0;
};

class PackageResolver : public PackageResolverInterface {
 public:
  // Takes in an unowned connection to boot arguments. boot_args must outlive PackageResolver.
  explicit PackageResolver(fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args)
      : boot_args_(boot_args) {}
  // PackageResolverInterface implementation.
  //
  // This will resolve the package, discover a driver inside the package,
  // and return the resulting Driver object.
  // At the moment this function assumes that the package will only have
  // one file in /lib/ and it assumes that this file is the package's driver.
  zx::status<std::unique_ptr<Driver>> FetchDriver(const std::string& package_url) override;

 private:
  struct FetchDriverVmoResult {
    std::string libname;
    zx::vmo vmo;
  };
  // Fetches the driver shared library from |package_url|.
  zx::status<FetchDriverVmoResult> FetchDriverVmo(const std::string& package_url);

  // Connects to the package resolver service if not already connected.
  zx_status_t ConnectToResolverService();
  // Creates the directory client for |package_url|.
  zx::status<fidl::WireSyncClient<fuchsia_io::Directory>> Resolve(
      const std::string_view& package_url);
  zx::status<FetchDriverVmoResult> LoadDriverPackage(
      fidl::WireSyncClient<fuchsia_io::Directory>* package_dir);
  zx::status<std::string> GetDriverLibname(fidl::WireSyncClient<fuchsia_io::Directory>* lib_dir);

  fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args_;
  fidl::WireSyncClient<fuchsia_pkg::PackageResolver> resolver_client_;
};

}  // namespace internal

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_PACKAGE_RESOLVER_H_
