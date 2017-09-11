// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPLICATION_SRC_BOOTSTRAP_DELEGATING_APPLICATION_LOADER_H_
#define APPLICATION_SRC_BOOTSTRAP_DELEGATING_APPLICATION_LOADER_H_

#include "lib/app/fidl/application_launcher.fidl.h"
#include "lib/app/fidl/application_loader.fidl.h"
#include "garnet/bin/bootstrap/config.h"
#include "lib/fxl/macros.h"

namespace bootstrap {

// TODO(rosswang): Ideally this would be reusable from scopes other than
// bootstrap, but it's tricky to wire in a fallback loader. If the need arises,
// perhaps we might move this to modular/lib/fidl.

// This loader executes in the bootstrap environment, reads a config file, and
// can delegate mapped URI schemes to app loaders capable of handling them,
// falling back on the root app loader for unmapped schemes.
class DelegatingApplicationLoader : public app::ApplicationLoader {
 public:
  explicit DelegatingApplicationLoader(
      Config::ServiceMap delegates,
      app::ApplicationLauncher* delegate_launcher,
      app::ApplicationLoaderPtr fallback);
  ~DelegatingApplicationLoader() override;

  // |ApplicationLoader|:
  void LoadApplication(
      const fidl::String& url,
      const ApplicationLoader::LoadApplicationCallback& callback) override;

 private:
  struct ApplicationLoaderRecord {
    app::ApplicationLaunchInfoPtr launch_info;
    app::ApplicationLoaderPtr loader;
    app::ApplicationControllerPtr controller;
  };

  void StartDelegate(ApplicationLoaderRecord* record);

  // indexed by URL. This ignores differentiation by args but is on par with the
  // bootstrap app implementation.
  std::unordered_map<std::string, ApplicationLoaderRecord> delegate_instances_;

  app::ApplicationLauncher* delegate_launcher_;
  app::ApplicationLoaderPtr fallback_;

  // indexed by scheme. ApplicationLoaderRecord instances are owned by
  // delegate_instances_.
  std::unordered_map<std::string, ApplicationLoaderRecord*>
      delegates_by_scheme_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DelegatingApplicationLoader);
};

}  // namespace bootstrap

#endif  // APPLICATION_SRC_BOOTSTRAP_DELEGATING_APPLICATION_LOADER_H_
