// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/view_manager/view_impl.h"
#include "apps/mozart/src/view_manager/view_registry1.h"

namespace view_manager {

class ViewImpl1 : public ViewImpl {
 public:
  explicit ViewImpl1(ViewRegistry1* registry);

 private:
  // Implement ViewImpl pure virtual methods.
  void OnSetState() override;

  // |View|
  void CreateScene(fidl::InterfaceRequest<mozart::Scene> scene) override;
};

}  // namespace view_manager
