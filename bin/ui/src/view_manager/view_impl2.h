// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "apps/mozart/src/view_manager/view_impl.h"
#include "apps/mozart/src/view_manager/view_registry2.h"

namespace view_manager {

class ViewImpl2 : public ViewImpl {
 public:
  explicit ViewImpl2(ViewRegistry2* registry, mozart2::SessionPtr session);

 private:
  // Implement ViewImpl pure virtual methods.
  void OnSetState() override;

  // |View|
  void CreateSession(
      ::fidl::InterfaceRequest<mozart2::Session> session,
      ::fidl::InterfaceHandle<mozart2::SessionListener> listener) override;

  void PopulateSession();
  ViewRegistry2* registry() { return static_cast<ViewRegistry2*>(registry_); }

  mozart2::SessionPtr session_;
};

}  // namespace view_manager
