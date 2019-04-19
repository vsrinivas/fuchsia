// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/loader/package_loader.h"

#include <fcntl.h>
#include <trace/event.h>
#include <zircon/status.h>

#include <utility>

#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/io/fd.h"
#include "lib/fsl/vmo/file.h"
#include "src/lib/fxl/logging.h"
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
  bool parsed = false;

  if (FuchsiaPkgUrl::IsFuchsiaPkgScheme(url)) {
    parsed = fuchsia_url.Parse(url);
  } else {
    parsed =
        fuchsia_url.Parse("fuchsia-pkg://fuchsia.com/" + GetPathFromURL(url));
  }

  // If the url isn't valid after our attempt at fix-up, bail.
  if (!parsed) {
    FXL_LOG(ERROR) << "Cannot load " << url << " because the URL is not valid.";
    callback(nullptr);
    return;
  }

  package.resolved_url = fuchsia_url.ToString();

  fxl::UniqueFD package_dir(
      open(fuchsia_url.pkgfs_dir_path().c_str(), O_DIRECTORY | O_RDONLY));
  if (!package_dir.is_valid()) {
    FXL_VLOG(1) << "Could not open directory " << fuchsia_url.pkgfs_dir_path()
                << " " << strerror(errno);
    callback(nullptr);
    return;
  }

  // Why does this method have un-reportable error conditions?
  zx::channel directory =
      fsl::CloneChannelFromFileDescriptor(package_dir.get());
  if (!directory) {
    FXL_LOG(ERROR) << "Could not clone directory "
                   << fuchsia_url.pkgfs_dir_path();
    callback(nullptr);
    return;
  }
  package.directory = std::move(directory);

  if (!fuchsia_url.resource_path().empty()) {
    if (!LoadResource(package_dir, fuchsia_url.resource_path(), package)) {
      FXL_LOG(ERROR) << "Could not load package resource "
                     << fuchsia_url.resource_path() << " from " << url;
      callback(nullptr);
      return;
    }
  }

  callback(fidl::MakeOptional(std::move(package)));
}

void PackageLoader::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::Loader> request) {
  bindings_.AddBinding(this, std::move(request));
}

bool PackageLoader::LoadResource(const fxl::UniqueFD& dir,
                                 const std::string path,
                                 fuchsia::sys::Package& package) {
  fsl::SizedVmo resource;
  if (!fsl::VmoFromFilenameAt(dir.get(), path, &resource)) {
    return false;
  }

  if (resource.ReplaceAsExecutable(zx::handle()) != ZX_OK)
    return false;

  resource.vmo().set_property(ZX_PROP_NAME, path.c_str(), path.length());
  package.data = fidl::MakeOptional(std::move(resource).ToTransport());

  return true;
}

}  // namespace component
