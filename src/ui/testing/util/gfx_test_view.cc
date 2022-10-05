// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/util/gfx_test_view.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/fsl/handles/object_info.h"

namespace ui_testing {

void GfxTestView::CreateViewWithViewRef(zx::eventpair token,
                                        fuchsia::ui::views::ViewRefControl view_ref_control,
                                        fuchsia::ui::views::ViewRef view_ref) {
  scenic_ = mock_handles_->svc().Connect<fuchsia::ui::scenic::Scenic>();

  // Set up scenic session.
  fuchsia::ui::scenic::SessionEndpoints endpoints;
  fuchsia::ui::scenic::SessionPtr session;
  endpoints.set_session(session.NewRequest());
  fidl::InterfaceHandle<fuchsia::ui::scenic::SessionListener> session_listener;
  session_ = std::make_unique<scenic::Session>(std::move(session), session_listener.NewRequest());
  endpoints.set_session_listener(session_listener.Bind());
  scenic_->CreateSessionT(std::move(endpoints), /* unused */ [] {});

  session_->set_event_handler([this](const std::vector<fuchsia::ui::scenic::Event>& events) {
    for (const auto& event : events) {
      if (!event.is_gfx())
        continue;  // skip non-gfx events

      if (event.gfx().is_view_properties_changed()) {
        test_view_properties_ = event.gfx().view_properties_changed().properties;

        if (width() != 0 && height() != 0) {
          DrawContent();
        }
      }
    }
  });

  view_ref_ = fidl::Clone(view_ref);

  // Create test view.
  test_view_ = std::make_unique<scenic::View>(session_.get(), scenic::ToViewToken(std::move(token)),
                                              std::move(view_ref_control), std::move(view_ref),
                                              "test manager view");
  root_node_ = std::make_unique<scenic::EntityNode>(session_.get());
  test_view_->AddChild(*root_node_);

  PresentChanges();
}

uint32_t GfxTestView::width() {
  FX_CHECK(test_view_properties_);
  return static_cast<uint32_t>(test_view_properties_->bounding_box.max.x -
                               test_view_properties_->bounding_box.min.x);
}

uint32_t GfxTestView::height() {
  FX_CHECK(test_view_properties_);
  return static_cast<uint32_t>(test_view_properties_->bounding_box.max.y -
                               test_view_properties_->bounding_box.min.y);
}

void GfxTestView::DrawRectangle(int32_t x, int32_t y, int32_t z, uint32_t width, uint32_t height,
                                uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
  scenic::Rectangle pane_shape(session_.get(), static_cast<float>(width),
                               static_cast<float>(height));
  scenic::Material pane_material(session_.get());
  pane_material.SetColor(red, green, blue, alpha);

  scenic::ShapeNode pane_node(session_.get());
  pane_node.SetShape(pane_shape);
  pane_node.SetMaterial(pane_material);

  // On gfx, we translate the location of the center of the rect.
  auto translation_x = x + (width / 2);
  auto translation_y = y + (height / 2);
  pane_node.SetTranslation(static_cast<float>(translation_x), static_cast<float>(translation_y),
                           static_cast<float>(z));
  root_node_->AddChild(pane_node);
}

void GfxTestView::PresentChanges() {
  session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});
}

}  // namespace ui_testing
