// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/root_loader.h"

#include <fcntl.h>

#include <utility>

#include "garnet/bin/appmgr/cmx_metadata.h"
#include "garnet/bin/appmgr/url_resolver.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/io/fd.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"

namespace component {

RootLoader::RootLoader() = default;

RootLoader::~RootLoader() = default;

void RootLoader::LoadComponent(fidl::StringPtr url,
                               LoadComponentCallback callback) {
  std::string path = GetPathFromURL(url);
  if (path.empty()) {
    // If we see a scheme other than file://, it's an error. Other schemes are
    // handled by CreateComponent, which invokes the appropriate runner.
    FXL_LOG(ERROR) << "Cannot load " << url
                   << " because the scheme is not supported.";
    callback(nullptr);
    return;
  }

  // 1. Try to load the URL directly, if the path is valid. If the path is valid
  // but we cannot load the component, exit immediately.
  fxl::UniqueFD fd(open(path.c_str(), O_RDONLY));
  if (fd.is_valid() || path[0] == '/') {
    // 1a. If the URL points to a cmx file, load its package.
    std::string package_name = CmxMetadata::GetPackageNameFromCmxPath(path);
    if (!package_name.empty()) {
      if (!LoadComponentFromPkgfs(package_name, callback)) {
        FXL_LOG(ERROR) << "Could not load package from cmx path: " << url;
        callback(nullptr);
      }
      return;
    }

    // 1b. Load whatever the URL points to.
    if (!LoadComponentWithProcess(fd, path, callback)) {
      FXL_LOG(ERROR) << "Could not load url: " << url
                     << "; resource located at path, but it could not be "
                        "launched as a component.";
      callback(nullptr);
    }
    return;
  }

  // 2. Try to load the URL from /pkgfs.
  if (path.find('/') == std::string::npos &&
      LoadComponentFromPkgfs(path, callback)) {
    return;
  }

  // 3. Try to load the URL from /system, if we cannot from /pkgfs.
  for (const auto& entry : {"/system/bin", "/system/pkgs"}) {
    std::string qualified_path =
        fxl::Concatenate({fxl::StringView(entry), "/", path});
    fd.reset(open(qualified_path.c_str(), O_RDONLY));
    if (fd.is_valid()) {
      path = qualified_path;
      break;
    }
  }
  if (!fd.is_valid() || !LoadComponentWithProcess(fd, path, callback)) {
    FXL_LOG(ERROR) << "Could not load url: " << url;
    callback(nullptr);
  }
}

bool RootLoader::LoadComponentFromPkgfs(std::string& path,
                                        LoadComponentCallback callback) {
  // TODO(abarth): We're currently hardcoding version 0 of the package,
  // but we'll eventually need to do something smarter.
  std::string pkg_path = fxl::Concatenate({"/pkgfs/packages/", path, "/0"});
  fxl::UniqueFD fd(open(pkg_path.c_str(), O_DIRECTORY | O_RDONLY));
  if (fd.is_valid()) {
    zx::channel directory = fsl::CloneChannelFromFileDescriptor(fd.get());
    if (directory) {
      fuchsia::sys::Package package;
      package.directory = std::move(directory);
      package.resolved_url = fxl::Concatenate({"file://", pkg_path});
      callback(fidl::MakeOptional(std::move(package)));
      return true;
    }
  }
  return false;
}

bool RootLoader::LoadComponentWithProcess(fxl::UniqueFD& fd, std::string& path,
                                          LoadComponentCallback callback) {
  fsl::SizedVmo data;
  if (fsl::VmoFromFd(std::move(fd), &data)) {
    fuchsia::sys::Package package;
    package.data = fidl::MakeOptional(std::move(data).ToTransport());
    package.resolved_url = fxl::Concatenate({"file://", path});
    callback(fidl::MakeOptional(std::move(package)));
    return true;
  }
  return false;
}

void RootLoader::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::Loader> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace component
