// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_UI_INPUT_MANAGER_INPUT_MANAGER_APP_H_
#define SERVICES_UI_INPUT_MANAGER_INPUT_MANAGER_APP_H_

#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"
#include "apps/mozart/services/views/interfaces/view_associates.mojom.h"

namespace input_manager {

// Input manager application entry point.
class InputManagerApp : public mojo::ApplicationImplBase {
 public:
  InputManagerApp();
  ~InputManagerApp() override;

 private:
  // |ApplicationImplBase|:
  void OnInitialize() override;
  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override;

  mojo::StrongBindingSet<mojo::ui::ViewAssociate> input_associates_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputManagerApp);
};

}  // namespace input_manager

#endif  // SERVICES_UI_INPUT_MANAGER_INPUT_MANAGER_APP_H_
