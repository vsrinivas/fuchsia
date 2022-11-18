// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/screenshot/flatland_screenshot.h"

#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/allocation/allocator.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_import_export_tokens.h"
#include "src/ui/scenic/lib/flatland/buffers/util.h"
#include "src/ui/scenic/lib/utils/helpers.h"
#include "zircon/system/ulib/fbl/include/fbl/algorithm.h"

using allocation::Allocator;
using fuchsia::ui::composition::FrameInfo;
using fuchsia::ui::composition::GetNextFrameArgs;
using fuchsia::ui::composition::ScreenCaptureConfig;
using fuchsia::ui::composition::ScreenCaptureError;
using screen_capture::ScreenCapture;

namespace {

constexpr uint32_t kBufferIndex = 0;
constexpr auto kBytesPerPixel = 4;

}  // namespace

namespace screenshot {

FlatlandScreenshot::FlatlandScreenshot(
    std::unique_ptr<ScreenCapture> screen_capturer, std::shared_ptr<Allocator> allocator,
    fuchsia::math::SizeU display_size, int display_rotation,
    fit::function<void(FlatlandScreenshot*)> destroy_instance_function)
    : screen_capturer_(std::move(screen_capturer)),
      flatland_allocator_(allocator),
      display_size_(display_size),
      display_rotation_(display_rotation),
      destroy_instance_function_(std::move(destroy_instance_function)),
      weak_factory_(this) {
  zx_status_t status = fdio_service_connect("/svc/fuchsia.sysmem.Allocator",
                                            sysmem_allocator_.NewRequest().TakeChannel().release());
  FX_DCHECK(status == ZX_OK);

  FX_DCHECK(screen_capturer_);
  FX_DCHECK(flatland_allocator_);
  FX_DCHECK(sysmem_allocator_);
  FX_DCHECK(display_size_.width);
  FX_DCHECK(display_size_.height);
  FX_DCHECK(destroy_instance_function_);

  // Create event and wait for initialization purposes.
  status = zx::event::create(0, &init_event_);
  init_wait_ = std::make_shared<async::WaitOnce>(init_event_.get(), ZX_EVENT_SIGNALED);

  // Do all sysmem initialization up front.
  allocation::BufferCollectionImportExportTokens ref_pair =
      allocation::BufferCollectionImportExportTokens::New();

  // Create sysmem tokens.
  fuchsia::sysmem::BufferCollectionTokenSyncPtr local_token;
  sysmem_allocator_->AllocateSharedCollection(local_token.NewRequest());
  FX_DCHECK(status == ZX_OK);
  fuchsia::sysmem::BufferCollectionTokenSyncPtr dup_token;
  status = local_token->Duplicate(std::numeric_limits<uint32_t>::max(), dup_token.NewRequest());
  FX_DCHECK(status == ZX_OK);
  status = local_token->Sync();
  FX_DCHECK(status == ZX_OK);

  fuchsia::sysmem::BufferCollectionPtr buffer_collection;
  sysmem_allocator_->BindSharedCollection(std::move(local_token), buffer_collection.NewRequest());

  if (display_rotation_ == 90 || display_rotation_ == 270) {
    std::swap(display_size_.width, display_size_.height);
  }

  // We only need 1 buffer since it gets re-used on every Take() call.
  buffer_collection->SetConstraints(
      true, utils::CreateDefaultConstraints(/*buffer_count=*/1, display_size_.width,
                                            display_size_.height));

  // Initialize Flatland allocator state.
  fuchsia::ui::composition::RegisterBufferCollectionArgs args = {};
  args.set_export_token(std::move(ref_pair.export_token));
  args.set_buffer_collection_token(std::move(dup_token));
  args.set_usages(fuchsia::ui::composition::RegisterBufferCollectionUsages::SCREENSHOT);

  flatland_allocator_->RegisterBufferCollection(
      std::move(args),
      [](fuchsia::ui::composition::Allocator_RegisterBufferCollection_Result result) {
        FX_DCHECK(!result.is_err());
      });

  ScreenCaptureConfig sc_args;
  sc_args.set_import_token(std::move(ref_pair.import_token));
  sc_args.set_buffer_count(1);
  sc_args.set_size({display_size_.width, display_size_.height});

  switch (display_rotation_) {
    case 0:
      sc_args.set_rotation(fuchsia::ui::composition::Rotation::CW_0_DEGREES);
      break;
    // If the display in rotated by 90 degrees, we need to apply a clockwise rotation of 270 degrees
    // in order to cancel the overall rotation and render the correct screenshot.
    case 90:
      sc_args.set_rotation(fuchsia::ui::composition::Rotation::CW_270_DEGREES);
      break;
    case 180:
      sc_args.set_rotation(fuchsia::ui::composition::Rotation::CW_180_DEGREES);
      break;
      // If the display in rotated by 270 degrees, we need to apply a clockwise rotation of 90
      // degrees in order to cancel the overall rotation and render the correct screenshot.
    case 270:
      sc_args.set_rotation(fuchsia::ui::composition::Rotation::CW_90_DEGREES);
      break;
    default:
      FX_LOGS(ERROR) << "Invalid display rotation value: " << display_rotation_;
  }

  buffer_collection->WaitForBuffersAllocated(
      [weak_ptr = weak_factory_.GetWeakPtr(), buffer_collection = std::move(buffer_collection),
       sc_args = std::move(sc_args)](zx_status_t status,
                                     fuchsia::sysmem::BufferCollectionInfo_2 info) mutable {
        if (!weak_ptr) {
          return;
        }
        FX_DCHECK(status == ZX_OK);
        weak_ptr->buffer_collection_info_ = std::move(info);
        buffer_collection->Close();

        weak_ptr->screen_capturer_->Configure(
            std::move(sc_args),
            [weak_ptr = std::move(weak_ptr)](fpromise::result<void, ScreenCaptureError> result) {
              FX_DCHECK(!result.is_error());
              if (!weak_ptr) {
                return;
              }
              weak_ptr->init_event_.signal(0u, ZX_EVENT_SIGNALED);
            });
      });
}

FlatlandScreenshot::~FlatlandScreenshot() {}

void FlatlandScreenshot::Take(fuchsia::ui::composition::ScreenshotTakeRequest format,
                              TakeCallback callback) {
  // Check if there is already a Take() call pending. Either the setup is done (|init_wait_| is
  // signaled) or the setup is still in progress.
  //
  // If the setup is done, then a Take() call would set |callback_|.
  // If the setup is not done, then a Take() call would make |init_wait| pending.
  if (callback_ != nullptr || init_wait_->is_pending()) {
    FX_LOGS(ERROR) << "Screenshot::Take() already in progress, closing connection. Wait for return "
                      "before calling again.";
    destroy_instance_function_(this);
    return;
  }

  if (!utils::IsEventSignalled(init_event_, ZX_EVENT_SIGNALED)) {
    // Begin to asynchronously wait on the event. Retry the Take() call when the initialization is
    // complete.
    zx_status_t status = init_wait_->Begin(
        async_get_default_dispatcher(),
        [weak_ptr = weak_factory_.GetWeakPtr(), format = std::move(format),
         callback = std::move(callback)](async_dispatcher_t*, async::WaitOnce*, zx_status_t status,
                                         const zx_packet_signal_t* signal) mutable {
          if (!weak_ptr) {
            return;
          }
          FX_DCHECK(status == ZX_OK || status == ZX_ERR_CANCELED);

          // Retry the Take() call.
          weak_ptr->Take(std::move(format), std::move(callback));
        });
    FX_DCHECK(status == ZX_OK);
    return;
  }

  callback_ = std::move(callback);

  FX_DCHECK(!render_event_);
  zx::event dup;
  zx_status_t status = zx::event::create(0, &render_event_);
  FX_DCHECK(status == ZX_OK);
  render_event_.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);

  GetNextFrameArgs frame_args;
  frame_args.set_event(std::move(dup));

  screen_capturer_->GetNextFrame(std::move(frame_args), [](auto result) {});

  // Wait for the frame to render in an async fashion.
  render_wait_ = std::make_shared<async::WaitOnce>(render_event_.get(), ZX_EVENT_SIGNALED);
  status = render_wait_->Begin(
      async_get_default_dispatcher(), [weak_ptr = weak_factory_.GetWeakPtr()](
                                          async_dispatcher_t*, async::WaitOnce*, zx_status_t status,
                                          const zx_packet_signal_t*) mutable {
        FX_DCHECK(status == ZX_OK || status == ZX_ERR_CANCELED);
        if (!weak_ptr) {
          return;
        }
        weak_ptr->HandleFrameRender();

        // Release the buffer to allow for subsequent screenshots.
        weak_ptr->screen_capturer_->ReleaseFrame(kBufferIndex, [](auto result) {});
      });
  FX_DCHECK(status == ZX_OK);
}

void FlatlandScreenshot::HandleFrameRender() {
  FX_DCHECK(callback_);

  fuchsia::ui::composition::ScreenshotTakeResponse response;

  // Copy ScreenCapture output for inspection. Note that the stride of the buffer may be different
  // than the width of the image, if the width of the image is not a multiple of 64.
  //
  // For instance, is the original image were 1024x600, the new width is 600. 600*4=2400 bytes,
  // which is not a multiple of 64. The next multiple would be 2432, which would mean the buffer
  // is actually a 608x1024 "pixel" buffer, since 2432/4=608. We must account for that 8 byte
  // padding when copying the bytes over to be inspected.
  FX_CHECK(ZX_OK == buffer_collection_info_.buffers[kBufferIndex].vmo.op_range(
                        ZX_VMO_OP_CACHE_CLEAN_INVALIDATE, 0,
                        buffer_collection_info_.settings.buffer_settings.size_bytes, nullptr, 0));

  const uint32_t pixels_per_row =
      utils::GetPixelsPerRow(buffer_collection_info_.settings, kBytesPerPixel, display_size_.width);
  uint32_t bytes_per_row = pixels_per_row * kBytesPerPixel;
  uint32_t valid_bytes_per_row = display_size_.width * kBytesPerPixel;

  zx::vmo response_vmo;
  if (bytes_per_row == valid_bytes_per_row) {
    zx_status_t status = buffer_collection_info_.buffers[kBufferIndex].vmo.duplicate(
        ZX_RIGHT_READ | ZX_RIGHT_MAP | ZX_RIGHT_TRANSFER | ZX_RIGHT_GET_PROPERTY, &response_vmo);
    FX_DCHECK(status == ZX_OK);
  } else {
    const auto response_vmo_size = display_size_.width * display_size_.height * kBytesPerPixel;
    FX_CHECK(ZX_OK == zx::vmo::create(response_vmo_size, 0, &response_vmo));
    uint8_t* response_vmo_base;
    FX_CHECK(ZX_OK == zx::vmar::root_self()->map(ZX_VM_PERM_WRITE | ZX_VM_PERM_READ, 0,
                                                 response_vmo, 0, response_vmo_size,
                                                 reinterpret_cast<uintptr_t*>(&response_vmo_base)));
    flatland::MapHostPointer(
        buffer_collection_info_, kBufferIndex,
        [&response_vmo_base, bytes_per_row, display_size = display_size_, valid_bytes_per_row,
         response_vmo_size](uint8_t* vmo_host, uint32_t num_bytes) {
          for (size_t i = 0; i < display_size.height; ++i) {
            FX_DCHECK(i * display_size.width * kBytesPerPixel < response_vmo_size);
            memcpy(&response_vmo_base[i * display_size.width * kBytesPerPixel],
                   &vmo_host[i * bytes_per_row], valid_bytes_per_row);
          }
        });

    FX_CHECK(ZX_OK == zx_cache_flush(response_vmo_base, response_vmo_size,
                                     ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE));
  }

  response.set_vmo(std::move(response_vmo));
  response.set_size({display_size_.width, display_size_.height});
  callback_(std::move(response));

  callback_ = nullptr;
  render_event_.reset();
}

}  // namespace screenshot
