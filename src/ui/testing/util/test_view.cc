// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/util/test_view.h"

#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>
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
}

void TestView::DrawContent() {
  FX_CHECK(width() > 0);
  FX_CHECK(height() > 0);

  if (content_type_ == ContentType::COORDINATE_GRID) {
    DrawCoordinateGrid();
  } else {
    DrawSimpleBackground();
  }

  PresentChanges();
}

void TestView::DrawCoordinateGrid() {
  const uint32_t view_width = width();
  const uint32_t view_height = height();

  FX_LOGS(INFO) << "Test view width " << view_width;
  FX_LOGS(INFO) << "Test view height " << view_height;

  const uint32_t pane_width =
      static_cast<uint32_t>(std::ceil(static_cast<float>(view_width) / 2.f));

  const uint32_t pane_height =
      static_cast<uint32_t>(std::ceil(static_cast<float>(view_height) / 2.f));

  for (uint8_t i = 0; i < 2; i++) {
    for (uint8_t j = 0; j < 2; j++) {
      // Compute width and height as integers.
      DrawRectangle(/* x = */ i * pane_width,
                    /* y = */ j * pane_height,
                    /* z = */ -20,
                    /* width = */ pane_width,
                    /* height = */ pane_height,
                    /* red = */ i * 255,
                    /* green = */ 0,
                    /* blue = */ j * 255,
                    /* alpha = */ 255);
    }
  }

  DrawRectangle(/* x = */ 3 * view_width / 8,
                /* y = */ 3 * view_height / 8,
                /* z = */ -40,
                /* width = */ view_width / 4,
                /* height = */ view_height / 4,
                /* red = */ 0,
                /* green = */ 255,
                /* blue = */ 0,
                /* alpha = */ 255);
}

void TestView::DrawSimpleBackground() {
  DrawRectangle(/* x = */ 0,
                /* y = */ 0,
                /* z = */ 0,
                /* width = */ width(),
                /* height = */ height(),
                /* red = */ 0,
                /* green = */ 255,
                /* blue = */ 0,
                /* alpha = */ 255);
}

void TestView::CreateViewWithViewRef(zx::eventpair token,
                                     fuchsia::ui::views::ViewRefControl view_ref_control,
                                     fuchsia::ui::views::ViewRef view_ref) {
  FX_LOGS(ERROR) << "CreateViewWithViewRef() is not implemented";
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
