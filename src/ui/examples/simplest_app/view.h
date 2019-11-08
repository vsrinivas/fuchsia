// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_EXAMPLES_SIMPLEST_APP_VIEW_H_
#define SRC_UI_EXAMPLES_SIMPLEST_APP_VIEW_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/ui/scenic/cpp/resources.h>

#include "src/lib/ui/base_view/base_view.h"

namespace simplest_app {

class SimplestAppView : public scenic::BaseView {
 public:
  SimplestAppView(scenic::ViewContext context, async::Loop* message_loop);
  ~SimplestAppView() = default;

  // |scenic::BaseView|.
  void OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) override;

  // |scenic::BaseView|.
  void OnInputEvent(fuchsia::ui::input::InputEvent event) override;

 private:
  // |scenic::SessionListener|.
  void OnScenicError(std::string error) override {}

  void UpdateBackground();

  async::Loop* const message_loop_;

  scenic::ShapeNode background_;

  scenic::EmbeddedViewInfo embedded_view_info_;
  std::unique_ptr<scenic::ViewHolder> view_holder_;

  bool focused_;
};

}  // namespace simplest_app

#endif  // SRC_UI_EXAMPLES_SIMPLEST_APP_VIEW_H_
