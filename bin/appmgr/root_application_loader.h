// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_APPMGR_ROOT_APPLICATION_LOADER_H_
#define GARNET_BIN_APPMGR_ROOT_APPLICATION_LOADER_H_

#include <functional>
#include <string>
#include <tuple>
#include <vector>

#include <zx/vmo.h>

#include "lib/app/fidl/application_loader.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fxl/macros.h"

namespace component {

class RootApplicationLoader : public ApplicationLoader {
 public:
  explicit RootApplicationLoader(std::vector<std::string> path);
  ~RootApplicationLoader() override;

  void LoadApplication(
      const f1dl::StringPtr& url,
      const ApplicationLoader::LoadApplicationCallback& callback) override;

  void AddBinding(f1dl::InterfaceRequest<ApplicationLoader> request);

 private:
  std::vector<std::string> path_;

  f1dl::BindingSet<ApplicationLoader> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RootApplicationLoader);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_ROOT_APPLICATION_LOADER_H_
