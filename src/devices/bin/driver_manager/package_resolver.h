// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_PACKAGE_RESOLVER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_PACKAGE_RESOLVER_H_

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.pkg/cpp/wire.h>
#include <lib/zx/status.h>
#include <lib/zx/vmo.h>
#include <zircon/types.h>

#include "src/devices/bin/driver_manager/driver.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

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
  // This takes a URL which should be a path to a driver shared library. This
  // will resolve the package, load the driver shared library,
  // and return the resulting Driver object.
  //
  // E.g of a URL: fuchsia-pkg://fuchsia.com/my-package#driver/my-driver.so
  zx::status<std::unique_ptr<Driver>> FetchDriver(const std::string& package_url) override;

 private:
  // Connects to the package resolver service if not already connected.
  zx_status_t ConnectToResolverService();

  // Creates the directory client for |package_url|.
  zx::status<fidl::WireSyncClient<fuchsia_io::Directory>> Resolve(
      const component::FuchsiaPkgUrl& package_url);

  zx::status<zx::vmo> LoadDriver(fidl::WireSyncClient<fuchsia_io::Directory>* package_dir,
                                 const component::FuchsiaPkgUrl& package_url);

  fidl::WireSyncClient<fuchsia_boot::Arguments>* boot_args_;
  fidl::WireSyncClient<fuchsia_pkg::PackageResolver> resolver_client_;
};

}  // namespace internal

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_PACKAGE_RESOLVER_H_
