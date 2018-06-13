// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_VIEW_FRAMEWORK_VIEW_PROVIDER_APP_H_
#define LIB_UI_VIEW_FRAMEWORK_VIEW_PROVIDER_APP_H_

#include <memory>

#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/ui/view_framework/view_provider_service.h"

namespace mozart {

// Provides a skeleton for an entire application that only offers
// a view provider service.
// This is only intended to be used for simple example programs.
class ViewProviderApp {
 public:
  explicit ViewProviderApp(ViewFactory factory);
  // Does not take ownership of |startup_context|.
  ViewProviderApp(fuchsia::sys::StartupContext* startup_context,
                  ViewFactory factory);
  ~ViewProviderApp();

 private:
  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;
  ViewProviderService service_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ViewProviderApp);
};

}  // namespace mozart

#endif  // LIB_UI_VIEW_FRAMEWORK_VIEW_PROVIDER_APP_H_
