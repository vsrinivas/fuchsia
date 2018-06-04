// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FUCHSIA_A11Y_SERVICE_H
#define FUCHSIA_A11Y_SERVICE_H

#include <fuchsia/ui/a11y/cpp/fidl.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/logging.h"

namespace examples {

class A11yClientApp: public fuchsia::ui::a11y::A11yClient {
 public:
  A11yClientApp() {}

  ~A11yClientApp() {}

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::ui::a11y::A11yClient> request) {
    bindings_.AddBinding(this, std::move(request));
  }

  void NotifyViewSelected() override {
    FXL_LOG(INFO) << "Flag has been captured.";
  }

 private:

  fidl::BindingSet<fuchsia::ui::a11y::A11yClient> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(A11yClientApp);
};

}

#endif //FUCHSIA_A11Y_SERVICE_H
