// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/package_updating_loader.h"

#include <fcntl.h>
#include <string>
#include <utility>

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fit/function.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>
#include "lib/fidl/cpp/optional.h"
#include "lib/fsl/io/fd.h"
#include "lib/fsl/vmo/file.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/substitute.h"
#include "lib/pkg_url/url_resolver.h"
#include "lib/svc/cpp/services.h"

namespace sysmgr {

PackageUpdatingLoader::PackageUpdatingLoader(
    std::unordered_set<std::string> update_dependency_urls,
    fuchsia::pkg::PackageResolverPtr resolver, async_dispatcher_t* dispatcher)
    : update_dependency_urls_(std::move(update_dependency_urls)),
      resolver_(std::move(resolver)),
      dispatcher_(dispatcher) {}

PackageUpdatingLoader::~PackageUpdatingLoader() = default;

void PackageUpdatingLoader::LoadUrl(fidl::StringPtr url,
                                    LoadUrlCallback callback) {
  // The updating loader can only update fuchsia-pkg URLs.
  component::FuchsiaPkgUrl fuchsia_url;
  bool parsed = false;
  if (component::FuchsiaPkgUrl::IsFuchsiaPkgScheme(url)) {
    parsed = fuchsia_url.Parse(url);
  } else {
    parsed = fuchsia_url.Parse("fuchsia-pkg://fuchsia.com/" +
                               component::GetPathFromURL(url));
  }
  if (!parsed) {
    PackageLoader::LoadUrl(url, callback);
    return;
  }

  // Avoid infinite reentry and cycles: Don't attempt to update the package
  // resolver or any dependent package. Contacting the package resolver may
  // require starting its component or a dependency, which would end up back
  // here.
  if (std::find(update_dependency_urls_.begin(), update_dependency_urls_.end(),
                url) != std::end(update_dependency_urls_)) {
    PackageLoader::LoadUrl(url, callback);
    return;
  }

  fuchsia::io::DirectoryPtr dir;
  auto dir_request = dir.NewRequest(dispatcher_);
  auto done_cb = [this, url, dir = std::move(dir),
                  callback](zx_status_t status) mutable {
    // TODO: only fail soft on NOT_FOUND?
    if (status != ZX_OK) {
      FXL_VLOG(1) << "Package update failed with "
                  << zx_status_get_string(status)
                  << ". Loading package without update: " << url;
    }
    PackageLoader::LoadUrl(url, callback);
  };

  fuchsia::pkg::UpdatePolicy update_policy;
  update_policy.fetch_if_absent = true;
  fidl::VectorPtr<fidl::StringPtr> selectors;
  selectors.reset({});
  resolver_->Resolve(fuchsia_url.package_path(), std::move(selectors),
                     std::move(update_policy), std::move(dir_request),
                     std::move(done_cb));
}

}  // namespace sysmgr
