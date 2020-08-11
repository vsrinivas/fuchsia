// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_SWAPCHAIN_DISPLAY_SWAPCHAIN_H_
#define SRC_UI_SCENIC_LIB_GFX_SWAPCHAIN_DISPLAY_SWAPCHAIN_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/zx/event.h>
#include <lib/zx/handle.h>
#include <lib/zx/vmo.h>
#include <zircon/pixelformat.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/lib/escher/flib/fence_listener.h"
#include "src/ui/lib/escher/resources/resource_manager.h"
#include "src/ui/lib/escher/resources/resource_recycler.h"
#include "src/ui/lib/escher/vk/vulkan_device_queues.h"
#include "src/ui/scenic/lib/display/display.h"
#include "src/ui/scenic/lib/display/display_controller_listener.h"
#include "src/ui/scenic/lib/gfx/swapchain/buffer_pool.h"
#include "src/ui/scenic/lib/gfx/swapchain/swapchain.h"
#include "src/ui/scenic/lib/gfx/sysmem.h"

#include <vulkan/vulkan.hpp>

namespace scenic_impl {
namespace gfx {

namespace test {
class DisplaySwapchainMockTest;
class DisplaySwapchainTest;
}  // namespace test

// DisplaySwapchain implements the Swapchain interface by using a Vulkan
// swapchain to present images to a physical display using the Zircon
// display controller API.
class DisplaySwapchain : public Swapchain {
 public:
  DisplaySwapchain(
      Sysmem* sysmem,
      std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller,
      std::shared_ptr<display::DisplayControllerListener> display_controller_listener,
      display::Display* display, escher::Escher* escher);
  ~DisplaySwapchain() override;

  // Callback to call on every vsync. Arguments are:
  // - the timestamp of the vsync.
  using OnVsyncCallback = fit::function<void(zx::time vsync_timestamp)>;

  // |Swapchain|
  bool DrawAndPresentFrame(fxl::WeakPtr<scheduling::FrameTimings> frame_timings,
                           size_t swapchain_index, const HardwareLayerAssignment& hla,
                           DrawCallback draw_callback) override;

  // Register a callback to be called on each vsync.
  // Only allows a single listener at a time.
  void RegisterVsyncListener(OnVsyncCallback on_vsync) {
    FX_CHECK(!on_vsync_);
    on_vsync_ = std::move(on_vsync);
  }

  // Remove the registered vsync listener.
  void UnregisterVsyncListener() { on_vsync_ = nullptr; }

  // Passes along color correction information to the display
  bool SetDisplayColorConversion(const ColorTransform& transform) override;

  static bool SetDisplayColorConversion(
      uint64_t display_id, fuchsia::hardware::display::ControllerSyncPtr& display_controller,
      const ColorTransform& transform);

  // Set the state for protected memory usage in |use_protected_memory_|. If there is a state
  // change to true, it reallocates |swapchain_buffers_| using protected memory.
  void SetUseProtectedMemory(bool use_protected_memory) override;

 private:
  friend class test::DisplaySwapchainTest;
  friend class test::DisplaySwapchainMockTest;

  struct FrameRecord {
    fxl::WeakPtr<scheduling::FrameTimings> frame_timings;
    size_t swapchain_index;

    escher::SemaphorePtr render_finished_escher_semaphore;
    uint64_t render_finished_event_id;
    zx::event render_finished_event;
    std::unique_ptr<async::Wait> render_finished_wait;

    // Event is signaled when the display is done using a frame.
    zx::event retired_event;
    uint64_t retired_event_id;

    bool presented = false;
    BufferPool::Framebuffer* buffer = nullptr;
    bool use_protected_memory = false;
  };
  std::unique_ptr<FrameRecord> NewFrameRecord(fxl::WeakPtr<scheduling::FrameTimings> frame_timings,
                                              size_t swapchain_index);

  bool InitializeFramebuffers(escher::ResourceRecycler* resource_recycler,
                              bool use_protected_memory);

  bool InitializeDisplayLayer();

  // When a frame is presented, the previously-presented frame becomes available
  // as a render target.
  void OnFrameRendered(size_t frame_index, zx::time render_finished_time);

  void OnVsync(uint64_t display_id, uint64_t timestamp, std::vector<uint64_t> image_ids,
               uint64_t cookie);

  // Sets the config which will be used for all imported images.
  void SetImageConfig(uint64_t layer_id, int32_t width, int32_t height, zx_pixel_format_t format);

  // Import a buffer collection token into the display controller so the constraints will be set on
  // it. Returns an id that can be used to refer to the collection.
  uint64_t ImportBufferCollection(fuchsia::sysmem::BufferCollectionTokenSyncPtr token);

  // Displays |buffer| on |display|. Will wait for |render_finished_event_id| to be signaled before
  // presenting. Will signal |frame_signal_event_id| when the image is retired.
  //
  // fuchsia::hardware::display::INVALID_DISP_ID can be passed for any of the event_ids if there is
  // no corresponding event to signal.
  void Flip(uint64_t layer_id, uint64_t buffer, uint64_t render_finished_event_id,
            uint64_t frame_signal_event_id);

  // A nullable Escher (good for testing) means some resources must be accessed
  // through its (valid) pointer.
  escher::Escher* const escher_ = nullptr;

  Sysmem* sysmem_;

  display::Display* const display_;
  uint64_t primary_layer_id_ = fuchsia::hardware::display::INVALID_DISP_ID;

  // The display controller driver binding.
  std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller_;
  std::shared_ptr<display::DisplayControllerListener> display_controller_listener_;

  // Ids used to talk to display controller. If we use |display_controller_|
  // in multiple places, we'll have to centralize this logic.
  uint64_t next_buffer_collection_id_ = fuchsia::hardware::display::INVALID_DISP_ID + 1;

  size_t next_frame_index_ = 0;
  size_t presented_frame_idx_ = 0;
  size_t outstanding_frame_count_ = 0;
  bool use_protected_memory_ = false;

  BufferPool swapchain_buffers_;
  // Optionally generated on the fly.
  BufferPool protected_swapchain_buffers_;

  std::vector<std::unique_ptr<FrameRecord>> frames_;

  vk::Device device_;
  vk::Queue queue_;

  OnVsyncCallback on_vsync_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplaySwapchain);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_SWAPCHAIN_DISPLAY_SWAPCHAIN_H_
