// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/examples/screen_recording/view_provider.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/composition/internal/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <lib/ui/scenic/cpp/view_creation_tokens.h>
#include <lib/ui/scenic/cpp/view_identity.h>
#include <zircon/status.h>

#include <cstdint>

#include <fbl/algorithm.h>

#include "src/ui/examples/screen_recording/screen_capture_helper.h"
#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/utils/helpers.h"

namespace screen_recording_example {

using fuchsia::ui::composition::RegisterBufferCollectionUsages;
using fuchsia::ui::composition::internal::FrameInfo;
using fuchsia::ui::composition::internal::ScreenCapture;
using fuchsia::ui::composition::internal::ScreenCaptureConfig;
using fuchsia::ui::composition::internal::ScreenCaptureError;

ViewProviderImpl::ViewProviderImpl(sys::ComponentContext* component_context)
    : context_(component_context) {
  context_->outgoing()->AddPublicService<fuchsia::ui::app::ViewProvider>(
      bindings_.GetHandler(this));
}

ViewProviderImpl::~ViewProviderImpl() {
  context_->outgoing()->RemovePublicService<fuchsia::ui::app::ViewProvider>();
}

void ViewProviderImpl::CreateView(zx::eventpair view_handle,
                                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>,
                                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) {
  FX_NOTIMPLEMENTED() << "CreateView() is not implemented.";
}

void ViewProviderImpl::CreateView2(fuchsia::ui::app::CreateView2Args args) {
  context_->svc()->Connect(sysmem_allocator_.NewRequest());
  context_->svc()->Connect(flatland_allocator_.NewRequest());

  // Set ContentId to be 2 above ContentIds used from num_buffers_ and 1 above kFilledRectId.
  kSquareRectId = {num_buffers_ + 2};

  flatland_connection_ =
      simple_present::FlatlandConnection::Create(context_.get(), "ScreenRecordingExample");
  flatland_ = flatland_connection_->flatland();
  parent_watcher_.set_error_handler([](zx_status_t status) {
    FX_LOGS(ERROR) << "Error from fuchsia::ui::composition::ParentViewportWatcher: "
                   << zx_status_get_string(status);
  });

  // Create ScreenCapture client.
  context_->svc()->Connect(screen_capture_.NewRequest());
  screen_capture_.set_error_handler(
      [](zx_status_t status) { FX_LOGS(ERROR) << "Lost connection to ScreenCapture"; });
  FX_DCHECK(screen_capture_);

  auto view_identity = scenic::NewViewIdentityOnCreation();

  flatland_->CreateView2(std::move(*args.mutable_view_creation_token()), std::move(view_identity),
                         /* protocols = */ {}, parent_watcher_.NewRequest());

  parent_watcher_->GetLayout([this](auto layout_info) {
    const auto [width, height] = layout_info.logical_size();
    display_width_ = width;
    display_height_ = height;
    half_display_width_ = display_width_ / 2;
    num_pixels_ = display_width_ * display_height_;

    SetUpFlatland();

    // Create buffer collection to render into for GetNextFrame() and to duplicate for
    // creating images.
    allocation::BufferCollectionImportExportTokens scr_ref_pair =
        allocation::BufferCollectionImportExportTokens::New();

    RegisterBufferCollectionUsages usage_types =
        RegisterBufferCollectionUsages::DEFAULT | RegisterBufferCollectionUsages::SCREENSHOT;

    fuchsia::sysmem::BufferCollectionInfo_2 sc_buffer_collection_info_ =
        CreateBufferCollectionInfo2WithConstraints(
            utils::CreateDefaultConstraints(num_buffers_, half_display_width_, display_height_),
            std::move(scr_ref_pair.export_token), flatland_allocator_.get(),
            sysmem_allocator_.get(), usage_types);

    fuchsia::ui::composition::ImageProperties image_properties = {};
    image_properties.set_size({half_display_width_, display_height_});

    // Initialize images with ContentId of their buffer index + 1.
    for (uint32_t i = 0; i < num_buffers_; i++) {
      fuchsia::ui::composition::BufferCollectionImportToken import_token_copy;
      scr_ref_pair.import_token.value.duplicate(ZX_RIGHT_SAME_RIGHTS, &import_token_copy.value);
      flatland_->CreateImage({i + 1}, std::move(import_token_copy), 0, std::move(image_properties));
      flatland_->SetImageBlendingFunction({i + 1}, fuchsia::ui::composition::BlendMode::SRC);
    }

    ScreenCaptureConfig sc_args;
    sc_args.set_import_token(std::move(scr_ref_pair.import_token));
    sc_args.set_image_size({half_display_width_, display_height_});

    screen_capture_->Configure(std::move(sc_args),
                               [this](fpromise::result<void, ScreenCaptureError> result) {
                                 if (result.is_ok()) {
                                   present_release_fences_.reserve(num_buffers_);
                                   ScreenCaptureCallback();
                                 }
                               });
    PresentCallback();
  });

  flatland_connection_->Present({}, [](auto) {});
}

void ViewProviderImpl::PresentCallback() {
  TRACE_DURATION("gfx", "Example::PresentCallback");
  DrawSquare();
  flatland_connection_->Present({}, [this](auto) { PresentCallback(); });
}

void ViewProviderImpl::ScreenCaptureCallback() {
  TRACE_DURATION("gfx", "Example::ScreenCaptureCallback");
  screen_capture_->GetNextFrame([this](fpromise::result<FrameInfo, ScreenCaptureError> result) {
    if (!result.is_ok()) {
      return;
    }
    FX_CHECK(result.value().has_buffer_index());
    uint64_t buffer_index = result.value().buffer_index();
    TRACE_DURATION("gfx", "GetNextFrameCallback", "buffer_index", buffer_index);
    FX_CHECK(buffer_index <= num_buffers_);

    flatland_->SetContent(kChildTransformId2, {buffer_index + 1});

    // Set up event to drop buffer when frame is presented.
    zx::event release_fence;
    zx::event::create(0, &release_fence);
    present_release_fences_[buffer_index] = utils::CopyEvent(release_fence);

    std::vector<zx::event> current_release_fences;
    current_release_fences.push_back(std::move(release_fence));
    fuchsia::ui::composition::PresentArgs present_args;
    present_args.set_release_fences(std::move(current_release_fences));
    present_args.set_unsquashable(true);

    auto wait = std::make_shared<async::WaitOnce>(present_release_fences_[buffer_index].get(),
                                                  ZX_EVENT_SIGNALED);
    zx_status_t status = wait->Begin(
        async_get_default_dispatcher(),
        [copy_ref = wait, token = std::move(*result.value().mutable_buffer_release_token()),
         buffer_index](async_dispatcher_t*, async::WaitOnce*, zx_status_t status,
                       const zx_packet_signal_t* signal) mutable {
          TRACE_DURATION("gfx", "ScreenCapture Frame Released", "buffer_index", buffer_index);
          FX_DCHECK(status == ZX_OK);
          // Drop token.
          return;
        });
    FX_DCHECK(status == ZX_OK);

    flatland_connection_->Present(std::move(present_args), [](auto) {});
    ScreenCaptureCallback();
  });
}

void ViewProviderImpl::SetUpFlatland() {
  flatland_->CreateTransform(kRootTransformId);
  flatland_->CreateTransform(kChildTransformId1);
  flatland_->CreateTransform(kChildTransformId2);
  flatland_->CreateTransform(kBouncingSquareTransformId);

  flatland_->SetTranslation(kChildTransformId1, {0, 0});
  flatland_->SetTranslation(kChildTransformId2, {static_cast<int32_t>(half_display_width_), 0});
  flatland_->SetTranslation(kBouncingSquareTransformId, {bs_.x, bs_.y});

  // Set up children of room transform.
  flatland_->SetRootTransform(kRootTransformId);
  flatland_->AddChild(kRootTransformId, kChildTransformId1);
  flatland_->AddChild(kRootTransformId, kChildTransformId2);
  flatland_->AddChild(kChildTransformId1, kBouncingSquareTransformId);

  // Set background of left section. Id is 1 above previously set for ScreenRecordingImages.
  const fuchsia::ui::composition::ContentId kFilledRectId = {num_buffers_ + 1};
  flatland_->CreateFilledRect(kFilledRectId);
  flatland_->SetImageBlendingFunction(kFilledRectId, fuchsia::ui::composition::BlendMode::SRC);
  flatland_->SetSolidFill(kFilledRectId, {0, 0, 0, 0}, {half_display_width_, display_height_});

  // Draw the bouncing square initially.
  flatland_->CreateFilledRect(kSquareRectId);
  flatland_->SetImageBlendingFunction(kSquareRectId, fuchsia::ui::composition::BlendMode::SRC);
  flatland_->SetSolidFill(kSquareRectId, {1, 1, 0, 1}, {bs_.size.width, bs_.size.height});

  flatland_->SetContent(kChildTransformId1, kFilledRectId);
  flatland_->SetContent(kBouncingSquareTransformId, kSquareRectId);
}

void ViewProviderImpl::DrawSquare() {
  bs_.x += bs_.x_speed;
  bs_.y += bs_.y_speed;

  flatland_->SetTranslation(kBouncingSquareTransformId, {bs_.x, bs_.y});

  CheckHit();
}

void ViewProviderImpl::CheckHit() {
  if (bs_.x + bs_.size.width >= half_display_width_ || bs_.x <= 0) {
    bs_.x_speed *= -1;
    flatland_->SetSolidFill(kSquareRectId, RandomColor(), {bs_.size.width, bs_.size.height});
  }

  if (bs_.y + bs_.size.height >= display_height_ || bs_.y <= 0) {
    bs_.y_speed *= -1;
    flatland_->SetSolidFill(kSquareRectId, RandomColor(), {bs_.size.width, bs_.size.height});
  }
}

fuchsia::ui::composition::ColorRgba ViewProviderImpl::RandomColor() {
  float r = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
  float g = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
  float b = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
  float a = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
  return {r, g, b, a};
}

}  // namespace screen_recording_example
