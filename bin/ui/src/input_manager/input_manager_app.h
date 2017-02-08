// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_INPUT_MANAGER_INPUT_MANAGER_APP_H_
#define APPS_MOZART_SRC_INPUT_MANAGER_INPUT_MANAGER_APP_H_

#include "application/lib/app/application_context.h"
#include "apps/mozart/services/views/view_associates.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/macros.h"

namespace input_manager {

class InputAssociate;

// Input manager application entry point.
class InputManagerApp {
 public:
  InputManagerApp();
  ~InputManagerApp();

 private:
  std::unique_ptr<modular::ApplicationContext> application_context_;
  fidl::BindingSet<mozart::ViewAssociate, std::unique_ptr<InputAssociate>>
      associate_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(InputManagerApp);
};

}  // namespace input_manager

#endif  // APPS_MOZART_SRC_INPUT_MANAGER_INPUT_MANAGER_APP_H_
