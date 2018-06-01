// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_SYSMGR_DELEGATING_LOADER_H_
#define GARNET_BIN_SYSMGR_DELEGATING_LOADER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include "garnet/bin/sysmgr/config.h"
#include "lib/fxl/macros.h"

namespace sysmgr {

// TODO(rosswang): Ideally this would be reusable from scopes other than
// sysmgr, but it's tricky to wire in a fallback loader. If the need arises,
// perhaps we might move this to modular/lib/fidl.

// This loader executes in the sysmgr environment, reads a config file, and
// can delegate mapped URI schemes to app loaders capable of handling them,
// falling back on the root app loader for unmapped schemes.
class DelegatingLoader : public fuchsia::sys::Loader {
 public:
  explicit DelegatingLoader(Config::ServiceMap delegates,
                            fuchsia::sys::Launcher* delegate_launcher,
                            fuchsia::sys::LoaderPtr fallback);
  ~DelegatingLoader() override;

  // |Loader|:
  void LoadComponent(fidl::StringPtr url,
                     LoadComponentCallback callback) override;

 private:
  struct LoaderRecord {
    fuchsia::sys::LaunchInfoPtr launch_info;
    fuchsia::sys::LoaderPtr loader;
    fuchsia::sys::ComponentControllerPtr controller;
  };

  void StartDelegate(LoaderRecord* record);

  // indexed by URL. This ignores differentiation by args but is on par with the
  // sysmgr app implementation.
  std::unordered_map<std::string, LoaderRecord> delegate_instances_;

  fuchsia::sys::Launcher* delegate_launcher_;
  fuchsia::sys::LoaderPtr fallback_;

  // indexed by scheme. LoaderRecord instances are owned by
  // delegate_instances_.
  std::unordered_map<std::string, LoaderRecord*> delegates_by_scheme_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DelegatingLoader);
};

}  // namespace sysmgr

#endif  // GARNET_BIN_SYSMGR_DELEGATING_LOADER_H_
