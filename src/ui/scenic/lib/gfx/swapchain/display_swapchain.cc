// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/swapchain/display_swapchain.h"

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/trace/event.h>

#include <fbl/auto_call.h>

#include "src/ui/lib/escher/escher.h"
#include "src/ui/lib/escher/flib/fence.h"
#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/util/bit_ops.h"
#include "src/ui/lib/escher/util/fuchsia_utils.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/gpu_mem.h"
#include "src/ui/scenic/lib/display/util.h"

#define VK_CHECK_RESULT(XXX) FX_CHECK(XXX.result == vk::Result::eSuccess)

namespace scenic_impl {
namespace gfx {

// TODO(SCN-400): Don't triple buffer.  This is done to avoid "tearing", but it
// wastes memory, and can result in the "permanent" addition of an extra Vsync
// period of latency.  An alternative would be to use an acquire fence; this
// saves memory, but can still result in the permanent extra latency.  Here's
// how:
//
// First, let's see how tearing occurs in the 2-framebuffer case.
//
// Let's say we have framebuffers A and B in a world that conveniently starts at
// some negative time, such that the first frame rendered into A has a target
// presentation time of 0ms, and the next frame is rendered into B with a target
// presentation time of 16ms.
//
// However, assume that frame being rendered into A takes a bit too long, so
// that instead of being presented at 0ms, it is instead presented at 16ms.  The
// frame to render into B has already been scheduled, and starts rendering at
// 8ms to hit the target presentation time of 16ms.  Even if it's fast, it
// cannot present at 16ms, because that frame has already been "claimed" by A,
// and so it is instead presented at 32ms.
//
// The tearing occurs when it is time to render A again.  We don't know that B
// has been deferred to present at 32ms.  So, we wake up at 24ms to render into
// A to hit the 32ms target.  Oops!
//
// The problem is that A is still being displayed from 16-32ms, until it is
// replaced by B at 32ms.  Thus, tearing.
//
// If you followed that, it should be clear both why triple-buffering fixes the
// tearing, and why it adds the frame of latency.
static const uint32_t kSwapchainImageCount = 3;

DisplaySwapchain::DisplaySwapchain(
    Sysmem* sysmem,
    std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller,
    std::shared_ptr<display::DisplayControllerListener> display_controller_listener,
    display::Display* display, escher::Escher* escher)
    : escher_(escher),
      sysmem_(sysmem),
      display_(display),
      display_controller_(display_controller),
      display_controller_listener_(display_controller_listener),
      swapchain_buffers_(/*count=*/0, /*environment=*/nullptr, /*use_protected_memory=*/false),
      protected_swapchain_buffers_(/*count=*/0, /*environment=*/nullptr,
                                   /*use_protected_memory=*/true) {
  FX_DCHECK(display);
  FX_DCHECK(sysmem);

  if (escher_) {
    device_ = escher_->vk_device();
    queue_ = escher_->device()->vk_main_queue();
    display_->Claim();
    frames_.resize(kSwapchainImageCount);

    if (!InitializeDisplayLayer()) {
      FX_LOGS(FATAL) << "Initializing display layer failed";
    }
    if (!InitializeFramebuffers(escher_->resource_recycler(), /*use_protected_memory=*/false)) {
      FX_LOGS(FATAL) << "Initializing buffers for display swapchain failed - check "
                        "whether fuchsia.sysmem.Allocator is available in this sandbox";
    }

    display_controller_listener_->SetOnVsyncCallback(
        fit::bind_member(this, &DisplaySwapchain::OnVsync));
    if ((*display_controller_)->EnableVsync(true) != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to enable vsync";
    }

  } else {
    device_ = vk::Device();
    queue_ = vk::Queue();

    display_->Claim();

    FX_VLOGS(2) << "Using a NULL escher in DisplaySwapchain; likely in a test.";
  }
}

bool DisplaySwapchain::InitializeFramebuffers(escher::ResourceRecycler* resource_recycler,
                                              bool use_protected_memory) {
  FX_CHECK(escher_);
  BufferPool::Environment environment = {
      .display_controller = display_controller_,
      .display = display_,
      .escher = escher_,
      .sysmem = sysmem_,
      .recycler = resource_recycler,
      .vk_device = device_,
  };
  BufferPool pool(kSwapchainImageCount, &environment, use_protected_memory);
  if ((*display_controller_)->SetLayerPrimaryConfig(primary_layer_id_, pool.image_config()) !=
      ZX_OK) {
    FX_LOGS(ERROR) << "Failed to set layer primary config";
  }
  if (use_protected_memory) {
    protected_swapchain_buffers_ = std::move(pool);
  } else {
    swapchain_buffers_ = std::move(pool);
  }
  return true;
}

DisplaySwapchain::~DisplaySwapchain() {
  if (!escher_) {
    display_->Unclaim();
    return;
  }

  // Turn off operations.
  if ((*display_controller_)->EnableVsync(false) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to disable vsync";
  }

  display_controller_listener_->SetOnVsyncCallback(nullptr);

  // A FrameRecord is now stale and will no longer receive the OnFramePresented
  // callback; OnFrameDropped will clean up and make the state consistent.
  for (size_t i = 0; i < frames_.size(); ++i) {
    const size_t idx = (i + next_frame_index_) % frames_.size();
    FrameRecord* record = frames_[idx].get();
    if (record && record->frame_timings && !record->frame_timings->finalized()) {
      if (record->render_finished_wait->is_pending()) {
        // There has not been an OnFrameRendered signal. The wait will be
        // destroyed when this function returns, and will never trigger the
        // OnFrameRendered callback. Trigger it here to make the state consistent
        // in FrameTimings. Record infinite time to signal unknown render time.
        record->frame_timings->OnFrameRendered(record->swapchain_index,
                                               scheduling::FrameTimings::kTimeDropped);
      }
      record->frame_timings->OnFrameDropped(record->swapchain_index);
    }
  }

  display_->Unclaim();

  if ((*display_controller_)->SetDisplayLayers(display_->display_id(), {}) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to configure display layers";
  } else {
    if ((*display_controller_)->DestroyLayer(primary_layer_id_) != ZX_OK) {
      FX_DLOGS(ERROR) << "Failed to destroy layer";
    }
  }

  swapchain_buffers_.Clear(display_controller_);
  protected_swapchain_buffers_.Clear(display_controller_);
}

std::unique_ptr<DisplaySwapchain::FrameRecord> DisplaySwapchain::NewFrameRecord(
    fxl::WeakPtr<scheduling::FrameTimings> frame_timings, size_t swapchain_index) {
  FX_DCHECK(frame_timings);
  FX_CHECK(escher_);
  auto render_finished_escher_semaphore = escher::Semaphore::NewExportableSem(device_);

  zx::event render_finished_event =
      GetEventForSemaphore(escher_->device(), render_finished_escher_semaphore);
  uint64_t render_finished_event_id =
      scenic_impl::ImportEvent(*display_controller_.get(), render_finished_event);

  if (!render_finished_escher_semaphore ||
      render_finished_event_id == fuchsia::hardware::display::INVALID_DISP_ID) {
    FX_LOGS(ERROR) << "DisplaySwapchain::NewFrameRecord() failed to create semaphores";
    return std::unique_ptr<FrameRecord>();
  }

  zx::event retired_event;
  zx_status_t status = zx::event::create(0, &retired_event);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "DisplaySwapchain::NewFrameRecord() failed to create retired event";
    return std::unique_ptr<FrameRecord>();
  }

  uint64_t retired_event_id = scenic_impl::ImportEvent(*display_controller_.get(), retired_event);
  if (retired_event_id == fuchsia::hardware::display::INVALID_DISP_ID) {
    FX_LOGS(ERROR) << "DisplaySwapchain::NewFrameRecord() failed to import retired event";
    return std::unique_ptr<FrameRecord>();
  }

  auto record = std::make_unique<FrameRecord>();
  record->frame_timings = frame_timings;
  record->swapchain_index = swapchain_index;
  record->render_finished_escher_semaphore = std::move(render_finished_escher_semaphore);
  record->render_finished_event_id = render_finished_event_id;
  record->retired_event = std::move(retired_event);
  record->retired_event_id = retired_event_id;

  record->render_finished_event = std::move(render_finished_event);
  record->render_finished_wait = std::make_unique<async::Wait>(
      record->render_finished_event.get(), escher::kFenceSignalled, ZX_WAIT_ASYNC_TIMESTAMP,
      [this, index = next_frame_index_](async_dispatcher_t* dispatcher, async::Wait* wait,
                                        zx_status_t status, const zx_packet_signal_t* signal) {
        OnFrameRendered(index, zx::time(signal->timestamp));
      });

  // TODO(SCN-244): What to do if rendering fails?
  record->render_finished_wait->Begin(async_get_default_dispatcher());

  return record;
}

bool DisplaySwapchain::DrawAndPresentFrame(fxl::WeakPtr<scheduling::FrameTimings> frame_timings,
                                           size_t swapchain_index,
                                           const HardwareLayerAssignment& hla,
                                           DrawCallback draw_callback) {
  FX_DCHECK(hla.swapchain == this);
  FX_DCHECK(frame_timings);

  // Create a record that can be used to notify |frame_timings| (and hence
  // ultimately the FrameScheduler) that the frame has been presented.
  //
  // There must not already exist a pending record.  If there is, it indicates
  // an error in the FrameScheduler logic (or somewhere similar), which should
  // not have scheduled another frame when there are no framebuffers available.
  auto& old_frame = frames_[next_frame_index_];
  if (old_frame) {
    if (auto timings = old_frame->frame_timings) {
      FX_CHECK(timings->finalized());
    }
    if (old_frame->retired_event.wait_one(ZX_EVENT_SIGNALED, zx::time(), nullptr) != ZX_OK) {
      FX_LOGS(WARNING) << "DisplaySwapchain::DrawAndPresentFrame rendering "
                          "into in-use backbuffer";
    }
    if (old_frame->buffer) {
      if (old_frame->use_protected_memory) {
        protected_swapchain_buffers_.Put(old_frame->buffer);
      } else {
        swapchain_buffers_.Put(old_frame->buffer);
      }
      old_frame->buffer = nullptr;
    }
  }

  auto& frame_record = frames_[next_frame_index_] = NewFrameRecord(frame_timings, swapchain_index);
  // Find the next framebuffer to render into, and other corresponding data.
  frame_record->buffer = use_protected_memory_ ? protected_swapchain_buffers_.GetUnused()
                                               : swapchain_buffers_.GetUnused();
  frame_record->use_protected_memory = use_protected_memory_;
  FX_CHECK(frame_record->buffer != nullptr);

  next_frame_index_ = (next_frame_index_ + 1) % kSwapchainImageCount;
  outstanding_frame_count_++;

  // Render the scene.
  size_t num_hardware_layers = hla.items.size();
  // TODO(SCN-1088): handle more hardware layers.
  FX_DCHECK(num_hardware_layers == 1);

  // TODO(SCN-1098): we'd like to validate that the layer ID is supported
  // by the display/display-controller, but the DisplayManager API doesn't
  // currently expose it, and rather than hack in an accessor for |layer_id_|
  // we should fix this "properly", whatever that means.
  // FX_DCHECK(hla.items[0].hardware_layer_id is supported by display);
  for (size_t i = 0; i < num_hardware_layers; ++i) {
    TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() draw");

    // A single semaphore is sufficient to guarantee that all images have been
    // rendered, so only provide the semaphore when rendering the image for
    // the final layer.
    escher::SemaphorePtr render_finished_escher_semaphore =
        (i + 1 == num_hardware_layers) ? frame_record->render_finished_escher_semaphore
                                       : escher::SemaphorePtr();
    // TODO(SCN-1088): handle more hardware layers: the single image from
    // buffer.escher_image is not enough; we need one for each layer.
    draw_callback(frame_timings->target_presentation_time(), frame_record->buffer->escher_image,
                  hla.items[i], escher::SemaphorePtr(), render_finished_escher_semaphore);
  }

  // When the image is completely rendered, present it.
  TRACE_DURATION("gfx", "DisplaySwapchain::DrawAndPresent() present");

  Flip(display_->display_id(), frame_record->buffer->id, frame_record->render_finished_event_id,
       frame_record->retired_event_id);

  if ((*display_controller_)->ReleaseEvent(frame_record->render_finished_event_id) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to release display controller event.";
  }
  if ((*display_controller_)->ReleaseEvent(frame_record->retired_event_id) != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to release display controller event.";
  }
  return true;
}

bool DisplaySwapchain::SetDisplayColorConversion(
    uint64_t display_id, fuchsia::hardware::display::ControllerSyncPtr& display_controller,
    const ColorTransform& transform) {
  // Attempt to apply color conversion.
  zx_status_t status = display_controller->SetDisplayColorConversion(
      display_id, transform.preoffsets, transform.matrix, transform.postoffsets);
  if (status != ZX_OK) {
    FX_LOGS(WARNING)
        << "DisplaySwapchain:SetDisplayColorConversion failed, controller returned status: "
        << status;
    return false;
  }

  // Now check the config.
  fuchsia::hardware::display::ConfigResult result;
  std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
  display_controller->CheckConfig(/*discard=*/false, &result, &ops);

  bool client_color_conversion_required = false;
  if (result != fuchsia::hardware::display::ConfigResult::OK) {
    client_color_conversion_required = true;
  }

  for (const auto& op : ops) {
    if (op.opcode == fuchsia::hardware::display::ClientCompositionOpcode::CLIENT_COLOR_CONVERSION) {
      client_color_conversion_required = true;
      break;
    }
  }

  if (client_color_conversion_required) {
    // Clear config by calling |CheckConfig| once more with "discard" set to true.
    display_controller->CheckConfig(/*discard=*/true, &result, &ops);
    // TODO (24591): Implement scenic software fallback for color correction.
    FX_LOGS(ERROR) << "Software fallback for color conversion not implemented.";
    return false;
  }

  return true;
}

bool DisplaySwapchain::SetDisplayColorConversion(const ColorTransform& transform) {
  FX_CHECK(display_);
  uint64_t display_id = display_->display_id();
  return SetDisplayColorConversion(display_id, *display_controller_, transform);
}

void DisplaySwapchain::SetUseProtectedMemory(bool use_protected_memory) {
  if (use_protected_memory == use_protected_memory_)
    return;

  // Allocate protected memory buffers lazily and once only.
  // TODO(35785): Free this memory chunk when we no longer expect protected memory.
  if (use_protected_memory && protected_swapchain_buffers_.empty()) {
    InitializeFramebuffers(escher_->resource_recycler(), use_protected_memory);
  }

  use_protected_memory_ = use_protected_memory;
}

bool DisplaySwapchain::InitializeDisplayLayer() {
  zx_status_t create_layer_status;
  zx_status_t transport_status =
      (*display_controller_)->CreateLayer(&create_layer_status, &primary_layer_id_);
  if (create_layer_status != ZX_OK || transport_status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create layer, " << create_layer_status;
    return false;
  }

  zx_status_t status =
      (*display_controller_)->SetDisplayLayers(display_->display_id(), {primary_layer_id_});
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to configure display layers";
    return false;
  }
  return true;
}

void DisplaySwapchain::OnFrameRendered(size_t frame_index, zx::time render_finished_time) {
  FX_DCHECK(frame_index < kSwapchainImageCount);
  auto& record = frames_[frame_index];

  uint64_t frame_number = record->frame_timings ? record->frame_timings->frame_number() : 0u;
  // TODO(57725) Replace with more robust solution.
  uint64_t frame_trace_id = (record->use_protected_memory * 3) + frame_index + 1;

  TRACE_DURATION("gfx", "DisplaySwapchain::OnFrameRendered", "frame count", frame_number,
                 "frame index", frame_trace_id);
  TRACE_FLOW_END("gfx", "scenic_frame", frame_number);

  TRACE_FLOW_BEGIN("gfx", "present_image", frame_trace_id);

  FX_DCHECK(record);
  if (record->frame_timings) {
    record->frame_timings->OnFrameRendered(record->swapchain_index, render_finished_time);
    // See ::OnVsync for comment about finalization.
  }
}

void DisplaySwapchain::OnVsync(uint64_t display_id, uint64_t timestamp,
                               std::vector<uint64_t> image_ids, uint64_t cookie) {
  if (on_vsync_) {
    on_vsync_(zx::time(timestamp));
  }

  // Respond acknowledgement message to display controller.
  if (cookie) {
    (*display_controller_)->AcknowledgeVsync(cookie);
  }

  if (image_ids.empty()) {
    return;
  }

  // Currently, only a single layer is ever used
  FX_CHECK(image_ids.size() == 1);
  uint64_t image_id = image_ids[0];

  bool match = false;
  while (outstanding_frame_count_ && !match) {
    auto& record = frames_[presented_frame_idx_];
    match = record->buffer->id == image_id;

    // Don't double-report a frame as presented if a frame is shown twice
    // due to the next frame missing its deadline.
    if (!record->presented) {
      record->presented = true;

      if (match && record->frame_timings) {
        record->frame_timings->OnFramePresented(record->swapchain_index, zx::time(timestamp));
      } else {
        record->frame_timings->OnFrameDropped(record->swapchain_index);
      }
    }

    // Retaining the currently displayed frame allows us to differentiate
    // between a frame being dropped and a frame being displayed twice
    // without having to look ahead in the queue, so only update the queue
    // when we know that the display controller has progressed to the next
    // frame.
    //
    // Since there is no guaranteed order between a frame being retired here
    // and OnFrameRendered() for a given frame, and since both must be called
    // for the FrameTimings to be finalized, we don't immediately destroy the
    // FrameRecord. It will eventually be replaced by DrawAndPresentFrame(),
    // when a new frame is rendered into this index.
    if (!match) {
      presented_frame_idx_ = (presented_frame_idx_ + 1) % kSwapchainImageCount;
      outstanding_frame_count_--;
    }
  }
  FX_DCHECK(match) << "Unhandled vsync image_id=" << image_id;
}

void DisplaySwapchain::Flip(uint64_t layer_id, uint64_t buffer, uint64_t render_finished_event_id,
                            uint64_t signal_event_id) {
  zx_status_t status =
      (*display_controller_)
          ->SetLayerImage(layer_id, buffer, render_finished_event_id, signal_event_id);
  // TODO(SCN-244): handle this more robustly.
  FX_CHECK(status == ZX_OK) << "DisplaySwapchain::Flip failed";

  auto before = zx::clock::get_monotonic();
  status = (*display_controller_)->ApplyConfig();
  // TODO(SCN-244): handle this more robustly.
  FX_CHECK(status == ZX_OK) << "DisplaySwapchain::Flip failed. Waited "
                            << (zx::clock::get_monotonic() - before).to_msecs() << "msecs";
}

}  // namespace gfx
}  // namespace scenic_impl
