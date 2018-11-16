// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/loader/component_loader.h"

#include <fcntl.h>
#include <trace/event.h>

#include <utility>

#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/io/fd.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/substitute.h"
#include "lib/pkg_url/url_resolver.h"

namespace component {

ComponentLoader::ComponentLoader() = default;
ComponentLoader::~ComponentLoader() = default;

void ComponentLoader::LoadComponent(fidl::StringPtr url,
                                    LoadComponentCallback callback) {
  TRACE_DURATION("appmgr", "RootLoader::LoadComponent", "url", url.get());

  // 1. If the URL is a fuchsia-pkg:// scheme, we are launching a .cmx.
  if (FuchsiaPkgUrl::IsFuchsiaPkgScheme(url)) {
    FuchsiaPkgUrl fp;
    if (!fp.Parse(url)) {
      FXL_LOG(ERROR) << "Could not parse fuchsia-pkg://: " << url;
      callback(nullptr);
      return;
    }
    std::string pkgfs_dir_path = fp.pkgfs_dir_path();
    if (!LoadComponentFromPkgfs(std::move(fp), callback)) {
      FXL_LOG(ERROR) << "Could not load package from cmx: " << url;
      callback(nullptr);
    }
    return;
  }

  std::string path = GetPathFromURL(url);
  if (path.empty()) {
    // If we see a scheme other than file://, it's an error. Other
    // schemes are handled earlier, or by CreateComponent, which invokes the
    // appropriate runner.
    FXL_LOG(ERROR) << "Cannot load " << url
                   << " because the scheme is not supported.";
    callback(nullptr);
    return;
  }

  // 2. Try to load the URL from /pkgfs.
  LoadComponentFromPackage(path, callback);
}

void ComponentLoader::AddBinding(
    fidl::InterfaceRequest<fuchsia::sys::Loader> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ComponentLoader::LoadComponentFromPackage(const std::string& package_name,
                                               LoadComponentCallback callback) {
  TRACE_DURATION("appmgr", "ComponentLoader::LoadComponentFromPackage",
                 "package_name", package_name);
  FuchsiaPkgUrl package_url;
  // "package_url" may or may not have a cmx file. If it does, the file is
  // implicitly assumed to live at "meta/<package_name>.cmx". It's also possible
  // the package hosts a cmx-less component.
  const std::string url_str =
      fxl::Substitute("fuchsia-pkg://fuchsia.com/$0", package_name);
  if (!package_url.Parse(url_str)) {
    FXL_LOG(ERROR) << "Could not parse package url: " << url_str;
    callback(nullptr);
    return;
  }
  if (!LoadComponentFromPkgfs(std::move(package_url), callback)) {
    FXL_LOG(ERROR) << "Could not load package for package: " << package_name;
    callback(nullptr);
    return;
  }
}

// static
bool ComponentLoader::LoadPackage(const FuchsiaPkgUrl& resolved_url,
                                  LoadComponentCallback callback) {
  const std::string& pkg_path = resolved_url.pkgfs_dir_path();
  fxl::UniqueFD fd(open(pkg_path.c_str(), O_DIRECTORY | O_RDONLY));
  if (fd.is_valid()) {
    zx::channel directory = fsl::CloneChannelFromFileDescriptor(fd.get());
    if (directory) {
      fuchsia::sys::Package package;
      package.directory = std::move(directory);
      package.resolved_url = resolved_url.ToString();
      callback(fidl::MakeOptional(std::move(package)));
      return true;
    }
  }
  return false;
}

}  // namespace component
