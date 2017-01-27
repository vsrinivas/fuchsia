// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_COMPONENT_MANAGER_SRC_RUN_COMPONENT_APPLICATION_LOADER_IMPL_H_
#define APPS_COMPONENT_MANAGER_SRC_RUN_COMPONENT_APPLICATION_LOADER_IMPL_H_

#include "apps/modular/services/application/application_loader.fidl.h"
#include "apps/modular/services/component/component.fidl.h"
#include "lib/ftl/macros.h"

namespace component {

class ApplicationLoaderImpl : public modular::ApplicationLoader {
 public:
  ApplicationLoaderImpl(ComponentIndexPtr component_index)
      : component_index_(std::move(component_index)) {}

  void LoadApplication(
      const fidl::String& url,
      const ApplicationLoader::LoadApplicationCallback& callback) override;

 private:
  ComponentIndexPtr component_index_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ApplicationLoaderImpl);
};

}  // namespace component

#endif  // APPS_COMPONENT_MANAGER_SRC_RUN_COMPONENT_APPLICATION_LOADER_IMPL_H_
