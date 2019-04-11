// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/pkg/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>

#include <memory>
#include <vector>

namespace {

// Mock out the package resolver, which is required with auto_update_packages.
// We don't want to depend on the real package resolver because that would
// make for a non-hermetic test.
class PackageResolverMock : public fuchsia::pkg::PackageResolver {
 public:
  PackageResolverMock() : context_(sys::ComponentContext::Create()) {
    context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  }

  void Resolve(::std::string package_uri,
               ::std::vector<::std::string> selectors,
               fuchsia::pkg::UpdatePolicy update_policy,
               ::fidl::InterfaceRequest<fuchsia::io::Directory> dir,
               ResolveCallback callback) override {
    dir_channels_.push_back(dir.TakeChannel());
    callback(ZX_OK);
  }

 private:
  std::unique_ptr<sys::ComponentContext> context_;
  std::vector<zx::channel> dir_channels_;
  fidl::BindingSet<fuchsia::pkg::PackageResolver> bindings_;
};

}  // namespace

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);

  PackageResolverMock service;
  loop.Run();
  return 0;
}
