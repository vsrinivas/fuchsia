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
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/substitute.h"
#include "lib/svc/cpp/services.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/pkg_url/url_resolver.h"

namespace sysmgr {

PackageUpdatingLoader::PackageUpdatingLoader(
    std::unordered_set<std::string> update_dependency_urls,
    fuchsia::sys::ServiceProviderPtr service_provider,
    async_dispatcher_t* dispatcher)
    : update_dependency_urls_(std::move(update_dependency_urls)),
      service_provider_(std::move(service_provider)),
      dispatcher_(dispatcher),
      needs_reconnect_(true) {
  EnsureConnectedToResolver();
}

PackageUpdatingLoader::~PackageUpdatingLoader() = default;

void PackageUpdatingLoader::Bind(
    fidl::InterfaceRequest<fuchsia::sys::Loader> request) {
  bindings_.AddBinding(this, std::move(request), dispatcher_);
}

void PackageUpdatingLoader::LoadUrl(std::string url, LoadUrlCallback callback) {
  EnsureConnectedToResolver();

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
    PackageLoader::LoadUrl(url, std::move(callback));
    return;
  }

  // Avoid infinite reentry and cycles: Don't attempt to update the package
  // resolver or any dependent package. Contacting the package resolver may
  // require starting its component or a dependency, which would end up back
  // here.
  if (std::find(update_dependency_urls_.begin(), update_dependency_urls_.end(),
                url) != std::end(update_dependency_urls_)) {
    PackageLoader::LoadUrl(url, std::move(callback));
    return;
  }

  fuchsia::io::DirectoryPtr dir;
  auto dir_request = dir.NewRequest(dispatcher_);
  auto done_cb = [this, url, dir = std::move(dir),
                  callback = std::move(callback)](zx_status_t status) mutable {
    // TODO: only fail soft on NOT_FOUND?
    if (status != ZX_OK) {
      FXL_VLOG(1) << "Package update failed with "
                  << zx_status_get_string(status)
                  << ". Loading package without update: " << url;
    }
    PackageLoader::LoadUrl(url, std::move(callback));
  };

  fuchsia::pkg::UpdatePolicy update_policy;
  update_policy.fetch_if_absent = true;
  fidl::VectorPtr<std::string> selectors;
  selectors.reset({});

  // TODO: if the resolver became unavailable in between the start of this
  // method and the following call to Resolve, our reconnection logic won't have
  // had a chance to execute, so we'll still block our client's request
  // indefinitely. to resolve this we'll maybe need to change the API or undergo
  // some more significant refactoring.
  resolver_->Resolve(fuchsia_url.package_path(), std::move(selectors),
                     std::move(update_policy), std::move(dir_request),
                     std::move(done_cb));
}

void PackageUpdatingLoader::EnsureConnectedToResolver() {
  if (needs_reconnect_) {
    service_provider_->ConnectToService(fuchsia::pkg::PackageResolver::Name_,
                                        resolver_.NewRequest().TakeChannel());

    // the error handler is consumed when an error is encountered, so if we
    // need to reconnect then it means we need to reinstall the handler too
    resolver_.set_error_handler([this](zx_status_t status) {
      FXL_LOG(ERROR) << "Package resolver error handler triggered, marking as "
                        "needing reconnect. status="
                     << status;
      needs_reconnect_ = true;
    });

    needs_reconnect_ = false;
  }
}

}  // namespace sysmgr
