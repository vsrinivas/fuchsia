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

#include <fuchsia/cpp/component.h>
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"

namespace component {

class RootApplicationLoader : public ApplicationLoader {
 public:
  explicit RootApplicationLoader();
  ~RootApplicationLoader() override;

  void LoadApplication(fidl::StringPtr url,
                       LoadApplicationCallback callback) override;

  void AddBinding(fidl::InterfaceRequest<ApplicationLoader> request);

 private:
  fidl::BindingSet<ApplicationLoader> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RootApplicationLoader);
};

}  // namespace component

#endif  // GARNET_BIN_APPMGR_ROOT_APPLICATION_LOADER_H_
