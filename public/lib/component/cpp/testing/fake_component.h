// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_TESTING_FAKE_COMPONENT_H_
#define LIB_APP_CPP_TESTING_FAKE_COMPONENT_H_

#include <fs/pseudo-dir.h>
#include <fs/synchronous-vfs.h>
#include <lib/async/default.h>

#include "lib/component/cpp/testing/fake_launcher.h"

namespace component {
namespace testing {

// A fake component that implements FIDL interfaces.
class FakeComponent {
 public:
  FakeComponent();
  ~FakeComponent();

  // Adds the specified interface to the set of public interfaces.
  //
  // Adds a supported service with the given |service_name|, using the given
  // |interface_request_handler|, which should remain valid for the lifetime of
  // this object.
  //
  // A typical usage may be:
  //
  //   AddPublicService(foobar_bindings_.GetHandler(this));
  template <typename Interface>
  zx_status_t AddPublicService(
      fidl::InterfaceRequestHandler<Interface> handler,
      const std::string& service_name = Interface::Name_) const {
    return directory_->AddEntry(
        service_name.c_str(),
        fbl::AdoptRef(new fs::Service(
            [handler = std::move(handler)](zx::channel channel) {
              handler(fidl::InterfaceRequest<Interface>(std::move(channel)));
              return ZX_OK;
            })));
  }

  // Register this component with a FakeLauncher.
  void Register(std::string url, FakeLauncher& fake_launcher);

 private:
  fs::SynchronousVfs directory_vfs_;
  fbl::RefPtr<fs::PseudoDir> directory_;
};

}  // namespace testing
}  // namespace component

#endif  // LIB_APP_CPP_TESTING_FAKE_COMPONENT_H_
