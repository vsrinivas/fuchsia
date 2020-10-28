// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/sysmgr/package_updating_loader.h"

#include <fcntl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <string>
#include <utility>

#include <fbl/unique_fd.h>

#include "lib/fdio/fd.h"
#include "lib/fidl/cpp/optional.h"
#include "lib/zx/handle.h"
#include "src/lib/fsl/io/fd.h"
#include "src/lib/fsl/vmo/file.h"
#include "src/lib/fxl/strings/substitute.h"
#include "src/lib/pkg_url/url_resolver.h"

namespace sysmgr {

PackageUpdatingLoader::PackageUpdatingLoader(std::unordered_set<std::string> update_dependency_urls,
                                             fuchsia::sys::ServiceProviderPtr service_provider,
                                             async_dispatcher_t* dispatcher)
    : update_dependency_urls_(std::move(update_dependency_urls)),
      service_provider_(std::move(service_provider)),
      dispatcher_(dispatcher),
      needs_reconnect_(true) {
  EnsureConnectedToResolver();
}

PackageUpdatingLoader::~PackageUpdatingLoader() = default;

void PackageUpdatingLoader::Bind(fidl::InterfaceRequest<fuchsia::sys::Loader> request) {
  bindings_.AddBinding(this, std::move(request), dispatcher_);
}

void PackageUpdatingLoader::LoadUrl(std::string url, LoadUrlCallback callback) {
  EnsureConnectedToResolver();

  // The updating loader can only update fuchsia-pkg URLs.
  component::FuchsiaPkgUrl fuchsia_url;
  if (!fuchsia_url.Parse(url)) {
    FX_LOGS(ERROR) << "Invalid package URL " << url;
    callback(nullptr);
    return;
  }

  // Avoid infinite reentry and cycles: Don't attempt to update the package
  // resolver or any dependent package. Contacting the package resolver may
  // require starting its component or a dependency, which would end up back
  // here.
  if (std::find(update_dependency_urls_.begin(), update_dependency_urls_.end(), url) !=
      std::end(update_dependency_urls_)) {
    PackageLoader::LoadUrl(url, std::move(callback));
    return;
  }

  fuchsia::io::DirectoryPtr dir;
  auto dir_request = dir.NewRequest(dispatcher_);
  auto done_cb = [this, url, fuchsia_url, dir = std::move(dir), callback = std::move(callback)](
                     fuchsia::pkg::PackageResolver_Resolve_Result result) mutable {
    if (result.is_err()) {
      // TODO: only fail soft on NOT_FOUND?
      FX_VLOGS(1) << "Package update failed with " << zx_status_get_string(result.err())
                  << ". Loading package without update: " << url;
      PackageLoader::LoadUrl(url, std::move(callback));
      return;
    }

    fuchsia::sys::Package package;
    package.resolved_url = fuchsia_url.ToString();
    package.directory = dir.Unbind().TakeChannel();

    if (!fuchsia_url.resource_path().empty()) {
      if (!component::LoadPackageResource(fuchsia_url.resource_path(), package)) {
        FX_LOGS(ERROR) << "Could not load package resource " << fuchsia_url.resource_path()
                       << " from " << fuchsia_url.ToString();
        callback(nullptr);
        return;
      }
    }

    callback(fidl::MakeOptional(std::move(package)));
  };

  fuchsia::pkg::UpdatePolicy update_policy;
  update_policy.fetch_if_absent = true;
  std::vector<std::string> selectors;

  // TODO: if the resolver became unavailable in between the start of this
  // method and the following call to Resolve, our reconnection logic won't have
  // had a chance to execute, so we'll still block our client's request
  // indefinitely. to resolve this we'll maybe need to change the API or undergo
  // some more significant refactoring.
  resolver_->Resolve(fuchsia_url.package_path(), std::move(selectors), std::move(update_policy),
                     std::move(dir_request), std::move(done_cb));
}

void PackageUpdatingLoader::EnsureConnectedToResolver() {
  if (needs_reconnect_) {
    service_provider_->ConnectToService(fuchsia::pkg::PackageResolver::Name_,
                                        resolver_.NewRequest().TakeChannel());

    // the error handler is consumed when an error is encountered, so if we
    // need to reconnect then it means we need to reinstall the handler too
    resolver_.set_error_handler([this](zx_status_t status) {
      FX_LOGS(WARNING) << "Package resolver error handler triggered, marking "
                          "as needing reconnect. status="
                       << status;
      needs_reconnect_ = true;
    });

    needs_reconnect_ = false;
  }
}

}  // namespace sysmgr
