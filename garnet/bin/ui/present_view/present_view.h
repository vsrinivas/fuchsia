// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_PRESENT_VIEW_PRESENT_VIEW_H_
#define GARNET_BIN_UI_PRESENT_VIEW_PRESENT_VIEW_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/types.h>

#include <string>
#include <vector>

namespace present_view {

struct ViewInfo {
 public:
  std::string url;
  std::vector<std::string> arguments;
  std::string locale;
};

class PresentView {
 public:
  explicit PresentView(std::unique_ptr<sys::ComponentContext> context);

  // Launch a component using the specified info and ask it to be presented.
  bool Present(ViewInfo view_info, fit::function<void(zx_status_t)> on_view_error);

 private:
  std::unique_ptr<sys::ComponentContext> context_;

  fuchsia::sys::ComponentControllerPtr view_controller_;
  fuchsia::ui::policy::PresenterPtr presenter_;
  fuchsia::ui::app::ViewProviderPtr legacy_view_provider_;
  fuchsia::ui::views::ViewPtr view_;
};

}  // namespace present_view

#endif  // GARNET_BIN_UI_PRESENT_VIEW_PRESENT_VIEW_H_
