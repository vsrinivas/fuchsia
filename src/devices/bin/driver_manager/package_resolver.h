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

namespace internal {

class PackageResolverInterface {
 public:
  virtual ~PackageResolverInterface() = default;

  struct FetchDriverVmoResult {
    std::string libname;
    zx::vmo vmo;
  };
  // Fetches the driver shared library from |package_url|.
  virtual zx::status<FetchDriverVmoResult> FetchDriverVmo(const std::string& package_url) = 0;
};

class PackageResolver : public PackageResolverInterface {
 public:
  // PackageResolverInterface implementation.
  //
  // This will resolve the package, discover a driver inside the package,
  // and return the driver name and also a vmo representing the driver.
  // At the moment this function assumes that the package will only have
  // one file in /lib/ and it assumes that this file is the package's driver.
  zx::status<FetchDriverVmoResult> FetchDriverVmo(const std::string& package_url) override;

 private:
  // Connects to the package resolver service if not already connected.
  zx_status_t ConnectToResolverService();
  // Creates the directory client for |package_url|.
  zx::status<::fuchsia_io::Directory::SyncClient> Resolve(const std::string_view& package_url);
  zx::status<FetchDriverVmoResult> LoadDriverPackage(
      ::fuchsia_io::Directory::SyncClient* package_dir);
  zx::status<std::string> GetDriverLibname(::fuchsia_io::Directory::SyncClient* lib_dir);

  ::fuchsia_pkg::PackageResolver::SyncClient resolver_client_;
};

}  // namespace internal

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_PACKAGE_RESOLVER_H_
