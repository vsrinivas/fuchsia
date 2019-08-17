// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_UI_SIMPLEST_EMBEDDER_VIEW_H_
#define GARNET_EXAMPLES_UI_SIMPLEST_EMBEDDER_VIEW_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/scenic/cpp/resources.h>

namespace simplest_embedder {

// Example implementation of BaseView, designed to launch and embed an instance
// of shadertoy_client.
class ShadertoyEmbedderView : public scenic::BaseView {
 public:
  ShadertoyEmbedderView(scenic::ViewContext context, async::Loop* message_loop);
  ~ShadertoyEmbedderView() = default;

  void LaunchShadertoyClient();

  // |scenic::BaseView|.
  void OnPropertiesChanged(fuchsia::ui::gfx::ViewProperties old_properties) override;

  // |scenic::BaseView|.
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  // |scenic::BaseView|.
  void OnInputEvent(fuchsia::ui::input::InputEvent event) override;

 private:
  // |scenic::SessionListener|.
  void OnScenicError(std::string error) override {}

  async::Loop* const message_loop_;

  scenic::ShapeNode background_;

  scenic::EmbeddedViewInfo embedded_view_info_;
  std::unique_ptr<scenic::ViewHolder> view_holder_;

  bool focused_;
};

}  // namespace simplest_embedder

#endif  // GARNET_EXAMPLES_UI_SIMPLEST_EMBEDDER_VIEW_H_
