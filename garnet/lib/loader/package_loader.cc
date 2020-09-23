// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/loader/package_loader.h"

#include <fcntl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/status.h>

#include <utility>

#include "lib/fdio/fd.h"
#include "lib/fidl/cpp/optional.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/pkg_url/url_resolver.h"

namespace component {

PackageLoader::PackageLoader() = default;
PackageLoader::~PackageLoader() = default;

void PackageLoader::LoadUrl(std::string url, LoadUrlCallback callback) {
  TRACE_DURATION("appmgr", "PackageLoader::LoadUrl", "url", url);

  // package is our result. We're going to build it up iteratively.
  fuchsia::sys::Package package;

  // First we are going to resolve the package directory, if it is present. We
  // can't handle resources yet, because we may not have enough URL to do so.
  FuchsiaPkgUrl fuchsia_url;

  // If the url isn't valid after our attempt at fix-up, bail.
  if (!fuchsia_url.Parse(url)) {
    FX_LOGS(ERROR) << "Cannot load " << url << " because the URL is not valid.";
    callback(nullptr);
    return;
  }

  package.resolved_url = fuchsia_url.ToString();

  fbl::unique_fd package_dir(open(fuchsia_url.pkgfs_dir_path().c_str(), O_DIRECTORY | O_RDONLY));
  if (!package_dir.is_valid()) {
    FX_VLOGS(1) << "Could not open directory " << fuchsia_url.pkgfs_dir_path() << " "
                << strerror(errno);
    callback(nullptr);
    return;
  }

  zx_status_t status;
  if ((status = fdio_fd_transfer(package_dir.release(),
                                 package.directory.reset_and_get_address())) != ZX_OK) {
    FX_LOGS(ERROR) << "Could not release directory channel " << fuchsia_url.pkgfs_dir_path()
                   << " status=" << zx_status_get_string(status);
    callback(nullptr);
    return;
  }

  if (!fuchsia_url.resource_path().empty()) {
    if (!LoadPackageResource(fuchsia_url.resource_path(), package)) {
      FX_LOGS(ERROR) << "Could not load package resource " << fuchsia_url.resource_path()
                     << " from " << url;
      callback(nullptr);
      return;
    }
  }

  // Falling back to cached (on-disk) packages for package URLs not from fuchsia.com is probably
  // an error.
  if (fuchsia_url.host_name() != "fuchsia.com") {
    FX_LOGS(WARNING) << "Using /pkgfs/packages/" << fuchsia_url.package_name() << " as "
                     << fuchsia_url.ToString()
                     << " but since this is not a fuchsia.com URL this probably isn't what you "
                        "wanted. Consider serving the package in your package repo. See fxbug.dev/48818.";
  }

  callback(fidl::MakeOptional(std::move(package)));
}

void PackageLoader::AddBinding(fidl::InterfaceRequest<fuchsia::sys::Loader> request) {
  bindings_.AddBinding(this, std::move(request));
}

bool LoadPackageResource(const std::string& path, fuchsia::sys::Package& package) {
  fsl::SizedVmo resource;

  auto dirfd = fsl::OpenChannelAsFileDescriptor(std::move(package.directory));
  const bool got_resource = fsl::VmoFromFilenameAt(dirfd.get(), path, &resource);
  if (fdio_fd_transfer(dirfd.release(), package.directory.reset_and_get_address()) != ZX_OK) {
    return false;
  }
  if (!got_resource) {
    return false;
  }

  resource.vmo().set_property(ZX_PROP_NAME, path.c_str(), path.length());
  package.data = fidl::MakeOptional(std::move(resource).ToTransport());

  return true;
}

}  // namespace component
