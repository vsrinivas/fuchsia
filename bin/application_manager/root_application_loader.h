// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_APPLICATION_MANAGER_ROOT_APPLICATION_LOADER_H_
#define APPS_MODULAR_SRC_APPLICATION_MANAGER_ROOT_APPLICATION_LOADER_H_

#include <functional>
#include <string>
#include <tuple>
#include <vector>

#include <mx/vmo.h>

#include "apps/modular/services/application/application_loader.fidl.h"
#include "lib/ftl/macros.h"

namespace modular {

class RootApplicationLoader : public ApplicationLoader {
 public:
  explicit RootApplicationLoader(std::vector<std::string> path);
  ~RootApplicationLoader() override;

  void LoadApplication(
      const fidl::String& url,
      const ApplicationLoader::LoadApplicationCallback& callback) override;

 private:
  std::vector<std::string> path_;

  FTL_DISALLOW_COPY_AND_ASSIGN(RootApplicationLoader);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_APPLICATION_MANAGER_ROOT_APPLICATION_LOADER_H_
