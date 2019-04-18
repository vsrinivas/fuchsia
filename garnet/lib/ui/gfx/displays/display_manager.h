// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_MANAGER_H_
#define GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_MANAGER_H_

#include <lib/fit/function.h>
#include <lib/zx/event.h>
#include <zircon/pixelformat.h>

#include <cstdint>

#include "fuchsia/hardware/display/cpp/fidl.h"
#include "fuchsia/sysmem/cpp/fidl.h"
#include "garnet/lib/ui/gfx/displays/display.h"
#include "garnet/lib/ui/gfx/displays/display_watcher.h"
#include "lib/async/cpp/wait.h"
#include "src/lib/fxl/macros.h"

namespace scenic_impl {
namespace gfx {

// Provides support for enumerating available displays.
class DisplayManager {
 public:
  DisplayManager();
  ~DisplayManager();

  using VsyncCallback = fit::function<void(
      zx_time_t timestamp, const std::vector<uint64_t>& images)>;

  // Waits for the default display to become available then invokes the
  // callback.
  void WaitForDefaultDisplay(fit::closure callback);

  // Generates or releases an event ID that can be used with the display
  // interface. The event can be signaled even after ReleaseEvent if it was
  // referenced in a Flip that's pending.
  uint64_t ImportEvent(const zx::event& event);
  void ReleaseEvent(uint64_t id);

  // Sets the config which will be used for all imported images.
  void SetImageConfig(int32_t width, int32_t height, zx_pixel_format_t format);

  fuchsia::sysmem::BufferCollectionTokenSyncPtr CreateBufferCollection();
  fuchsia::sysmem::BufferCollectionSyncPtr GetCollectionFromToken(
      fuchsia::sysmem::BufferCollectionTokenSyncPtr token);

  // Import a buffer collection token into the display controller so the
  // contraints will be set on it. Returns an id that can be used to refer to
  // the collection.
  uint64_t ImportBufferCollection(
      fuchsia::sysmem::BufferCollectionTokenSyncPtr token);
  void ReleaseBufferCollection(uint64_t id);

  uint64_t ImportImage(uint64_t collection_id, uint32_t index);
  void ReleaseImage(uint64_t id);

  // Displays |buffer| on |display|. Will wait for |render_finished_event_id|
  // to be signaled before presenting. Will signal |frame_signal_event_id| when
  // the image is retired.
  //
  // fuchsia::hardware::display::invalidId can be passed for any of the
  // event_ids if there is no corresponding event to signal.
  void Flip(Display* display, uint64_t buffer,
            uint64_t render_finished_event_id, uint64_t frame_signal_event_id);

  // Gets information about the default display.
  // May return null if there isn't one.
  Display* default_display() const { return default_display_.get(); }

  // For testing.
  void SetDefaultDisplayForTests(std::unique_ptr<Display> display) {
    default_display_ = std::move(display);
  }

  // Enables display vsync events and sets the callback which handles them.
  bool EnableVsync(VsyncCallback vsync_cb);

  bool is_initialized() { return sysmem_allocator_.is_bound(); }

 private:
  void OnAsync(async_dispatcher_t* dispatcher, async::WaitBase* self,
               zx_status_t status, const zx_packet_signal_t* signal);
  async::WaitMethod<DisplayManager, &DisplayManager::OnAsync> wait_{this};

  void DisplaysChanged(::std::vector<fuchsia::hardware::display::Info> added,
                       ::std::vector<uint64_t> removed);
  void ClientOwnershipChange(bool has_ownership);

  zx::channel dc_device_;
  fuchsia::hardware::display::ControllerSyncPtr display_controller_;
  fidl::InterfacePtr<fuchsia::hardware::display::Controller> event_dispatcher_;
  zx_handle_t dc_channel_;  // display_controller_ owns the zx::channel

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;

  uint64_t next_event_id_ = fuchsia::hardware::display::invalidId + 1;
  uint64_t next_buffer_collection_id_ =
      fuchsia::hardware::display::invalidId + 1;

  DisplayWatcher display_watcher_;
  fit::closure display_available_cb_;
  std::unique_ptr<Display> default_display_;
  // A boolean indicating whether or not we have ownership of the display
  // controller (not just individual displays). The default is no.
  bool owns_display_controller_ = false;

  fuchsia::hardware::display::ImageConfig image_config_;
  uint64_t layer_id_;
  VsyncCallback vsync_cb_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayManager);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_MANAGER_H_
