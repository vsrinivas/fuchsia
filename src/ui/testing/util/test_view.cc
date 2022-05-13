// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/util/test_view.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/scenic/cpp/session.h>
#include <lib/ui/scenic/cpp/view_ref_pair.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/fsl/handles/object_info.h"

namespace ui_testing {

void TestView::Start(std::unique_ptr<component_testing::LocalComponentHandles> mock_handles) {
  FX_CHECK(mock_handles->outgoing()->AddPublicService(
               fidl::InterfaceRequestHandler<fuchsia::ui::app::ViewProvider>([this](auto request) {
                 view_provider_bindings_.AddBinding(this, std::move(request), dispatcher_);
               })) == ZX_OK);
  mock_handles_ = std::move(mock_handles);

  scenic_ = mock_handles_->svc().Connect<fuchsia::ui::scenic::Scenic>();
}

void TestView::CreateViewWithViewRef(zx::eventpair token,
                                     fuchsia::ui::views::ViewRefControl view_ref_control,
                                     fuchsia::ui::views::ViewRef view_ref) {
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

  // Request to present ; this will trigger dispatch of view properties.
  session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});
}

float TestView::width() {
  FX_CHECK(test_view_properties_);
  return test_view_properties_->bounding_box.max.x - test_view_properties_->bounding_box.min.x;
}

float TestView::height() {
  FX_CHECK(test_view_properties_);
  return test_view_properties_->bounding_box.max.y - test_view_properties_->bounding_box.min.y;
}

void TestView::DrawCoordinateGrid() {
  scenic::EntityNode root_node(session_.get());
  test_view_->AddChild(root_node);

  const float view_width = width();
  const float view_height = height();

  const float pane_width = view_width / 2;
  const float pane_height = view_height / 2;

  for (uint8_t i = 0; i < 2; i++) {
    for (uint8_t j = 0; j < 2; j++) {
      scenic::Rectangle pane_shape(session_.get(), pane_width, pane_height);
      scenic::Material pane_material(session_.get());
      pane_material.SetColor(i * 255, 0, j * 255, 255);

      scenic::ShapeNode pane_node(session_.get());
      pane_node.SetShape(pane_shape);
      pane_node.SetMaterial(pane_material);
      pane_node.SetTranslation((i + 0.5f) * pane_width, (j + 0.5f) * pane_height, -20);
      root_node.AddChild(pane_node);
    }
  }

  scenic::Rectangle pane_shape(session_.get(), view_width / 4, view_height / 4);
  scenic::Material pane_material(session_.get());
  pane_material.SetColor(0, 255, 0, 255);

  scenic::ShapeNode pane_node(session_.get());
  pane_node.SetShape(pane_shape);
  pane_node.SetMaterial(pane_material);
  pane_node.SetTranslation(0.5f * view_width, 0.5f * view_height, -40);
  root_node.AddChild(pane_node);
}

void TestView::DrawSimpleBackground() {
  scenic::Rectangle background_shape(session_.get(), width(), height());
  scenic::ShapeNode background_node(session_.get());
  background_node.SetShape(background_shape);
  background_node.SetTranslation({0, 0, 0});
  test_view_->AddChild(background_node);
}

void TestView::DrawContent() {
  FX_CHECK(test_view_properties_);

  if (content_type_ == ContentType::COORDINATE_GRID) {
    DrawCoordinateGrid();
  } else {
    DrawSimpleBackground();
  }

  session_->Present2(/*when*/ zx::clock::get_monotonic().get(), /*span*/ 0, [](auto) {});
}

void TestView::CreateView(zx::eventpair view_handle,
                          fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>,
                          fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) {
  FX_LOGS(ERROR) << "CreateView() is not implemented.";
}

void TestView::CreateView2(fuchsia::ui::app::CreateView2Args args) {
  FX_LOGS(ERROR) << "CreateView2() is not implemented.";
}

std::optional<zx_koid_t> TestView::GetViewRefKoid() {
  if (!view_ref_)
    return std::nullopt;

  return fsl::GetKoid(view_ref_->reference.get());
}

}  // namespace ui_testing
