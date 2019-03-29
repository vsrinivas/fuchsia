// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSMGR_PACKAGE_UPDATING_LOADER_H_
#define GARNET_BIN_SYSMGR_PACKAGE_UPDATING_LOADER_H_

#include <string>
#include <unordered_set>

#include <fuchsia/pkg/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/dispatcher.h>
#include <lib/fit/function.h>
#include "garnet/lib/loader/package_loader.h"
#include "src/lib/fxl/macros.h"
#include "src/lib/pkg_url/fuchsia_pkg_url.h"

namespace sysmgr {

// A component loader that updates a package (or installs it for the first time)
// before running a component in it. Requires a connection to the package
// resolver.
class PackageUpdatingLoader final : public component::PackageLoader {
 public:
  typedef fit::function<void(std::string)> DoneCallback;

  PackageUpdatingLoader(std::unordered_set<std::string> update_dependency_urls,
                        fuchsia::sys::ServiceProviderPtr service_provider,
                        async_dispatcher_t* dispatcher);
  ~PackageUpdatingLoader() override;

  void Bind(fidl::InterfaceRequest<fuchsia::sys::Loader> request);

  void LoadUrl(std::string url, LoadUrlCallback callback) override;

 private:
  const std::unordered_set<std::string> update_dependency_urls_;
  fuchsia::pkg::PackageResolverPtr resolver_;
  fuchsia::sys::ServiceProviderPtr service_provider_;
  fidl::BindingSet<fuchsia::sys::Loader> bindings_;
  async_dispatcher_t* const dispatcher_;  // Not owned.
  bool needs_reconnect_;

  void EnsureConnectedToResolver();

  FXL_DISALLOW_COPY_AND_ASSIGN(PackageUpdatingLoader);
};

}  // namespace sysmgr

#endif  // GARNET_BIN_SYSMGR_PACKAGE_UPDATING_LOADER_H_
