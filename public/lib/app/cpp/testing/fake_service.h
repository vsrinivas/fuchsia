// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_TESTING_FAKE_SERVICE_H_
#define LIB_APP_CPP_TESTING_FAKE_SERVICE_H_

#include "fake_launcher.h"

#include <fs/pseudo-dir.h>
#include <fs/synchronous-vfs.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>

#include "lib/fidl/cpp/binding.h"

namespace fuchsia {
namespace sys {
namespace testing {

// A fake Service that implements a FIDL |Interface|.
template <typename Interface>
class FakeService {
 public:
  FakeService(Interface* iface)
      : binding_(iface),
        directory_vfs_(async_get_default()),
        directory_(fbl::AdoptRef(new fs::PseudoDir())){};

  // Register with a FakeLauncher
  void Register(std::string url, FakeLauncher& fake_launcher) {
    fake_launcher.RegisterComponent(
        url,
        [&](fuchsia::sys::LaunchInfo launch_info,
            fidl::InterfaceRequest<fuchsia::sys::ComponentController> ctrl) {
          directory_->AddEntry(
              Interface::Name_,
              fbl::AdoptRef(new fs::Service([&](zx::channel channel) {
                binding_.Bind(
                    fidl::InterfaceRequest<Interface>(std::move(channel)));
                return ZX_OK;
              })));
          directory_vfs_.ServeDirectory(
              directory_, std::move(launch_info.directory_request));
        });
  }

 private:
  fidl::Binding<Interface> binding_;
  fs::SynchronousVfs directory_vfs_;
  fbl::RefPtr<fs::PseudoDir> directory_;
};

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia

#endif  // LIB_APP_CPP_TESTING_FAKE_SERVICE_H_
