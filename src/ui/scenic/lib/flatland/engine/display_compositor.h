// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_DISPLAY_COMPOSITOR_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_DISPLAY_COMPOSITOR_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/display/util.h"
#include "src/ui/scenic/lib/flatland/engine/engine_types.h"
#include "src/ui/scenic/lib/flatland/engine/release_fence_manager.h"

namespace flatland {

namespace test {
class DisplayCompositorSmokeTest;
class DisplayCompositorPixelTest;
class DisplayCompositorTest;
}  // namespace test

// The DisplayCompositor is responsible for compositing Flatland render data onto the display(s).
// It accomplishes this either by direct hardware compositing via the display controller
// interface, or rendering on the GPU via a custom renderer API. It also handles the
// registration of sysmem buffer collections and importation of images to both the
// display controller and the renderer via the BufferCollectionImporter interface. The
// BufferCollectionImporter interface is how Flatland instances communicate with the
// DisplayCompositor, providing it with the necessary data to render without exposing to Flatland
// the DisplayController or other dependencies.
class DisplayCompositor final : public allocation::BufferCollectionImporter {
 public:
  // TODO(fxbug.dev/66807): The DisplayCompositor has multiple parts of its code where usage of the
  // display controller is protected by locks, because of the multithreaded environment of flatland.
  // Ideally, we'd want the DisplayCompositor to have sole ownership of the display controller -
  // meaning that it would require a unique_ptr instead of a shared_ptr. But since access to the
  // real display controller is provided to clients via a shared_ptr, we take in a shared_ptr as a
  // parameter here. However, this could cause problems with our locking mechanisms, as other
  // display-controller clients could be accessing the same functions and/or state at the same time
  // as the DisplayCompositor without making use of locks.
  DisplayCompositor(
      async_dispatcher_t* dispatcher,
      std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller,
      const std::shared_ptr<Renderer>& renderer, fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator,
      BufferCollectionImportMode import_mode);

  ~DisplayCompositor() override;

  // |BufferCollectionImporter|
  bool ImportBufferCollection(
      allocation::GlobalBufferCollectionId collection_id,
      fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
      fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token) override;

  // |BufferCollectionImporter|
  void ReleaseBufferCollection(allocation::GlobalBufferCollectionId collection_id) override;

  // |BufferCollectionImporter|
  bool ImportBufferImage(const allocation::ImageMetadata& metadata) override;

  // |BufferCollectionImporter|
  void ReleaseBufferImage(allocation::GlobalImageId image_id) override;

  // Generates frame and presents it to display.  This may involve directly scanning out client
  // images, or it may involve first using the GPU to composite (some of) these images into a single
  // image which is then scanned out.
  void RenderFrame(uint64_t frame_number, zx::time presentation_time,
                   const std::vector<RenderData>& render_data_list,
                   std::vector<zx::event> release_fences,
                   scheduling::FrameRenderer::FramePresentedCallback callback);

  // Register a new display to the DisplayCompositor, which also generates the render targets to be
  // presented on the display when compositing on the GPU. If num_vmos is 0, this function will not
  // create any render targets for GPU composition for that display. The buffer collection info is
  // also returned back to the caller via an output parameter |out_collection_info|. This out
  // parameter is only allowed to be nullptr when num_vmos is 0. Otherwise, a valid handle to return
  // the buffer collection data is required. This function also returns the ID for the buffer
  // collection of the render targets.
  // TODO(fxbug.dev/59646): We need to figure out exactly how we want the display to anchor
  // to the Flatland hierarchy.
  allocation::GlobalBufferCollectionId AddDisplay(
      uint64_t display_id, DisplayInfo info, uint32_t num_vmos,
      fuchsia::sysmem::BufferCollectionInfo_2* out_collection_info);

 private:
  friend class test::DisplayCompositorSmokeTest;
  friend class test::DisplayCompositorPixelTest;
  friend class test::DisplayCompositorTest;

  // Notifies the compositor that a vsync has occurred, in response to a display configuration
  // applied by the compositor.  It is the compositor's responsibility to signal any release fences
  // corresponding to the frame identified by |frame_number|.
  void OnVsync(uint64_t display_id, uint64_t frame_number, zx::time timestamp);

  struct DisplayConfigResponse {
    // Whether or not the config can be successfully applied or not.
    fuchsia::hardware::display::ConfigResult result;
    // If the config is invalid, this vector will list all the operations
    // that need to be performed to make the config valid again.
    std::vector<fuchsia::hardware::display::ClientCompositionOp> ops;
  };

  struct FrameEventData {
    scenic_impl::DisplayEventId wait_id;
    scenic_impl::DisplayEventId signal_id;
    zx::event wait_event;
    zx::event signal_event;
  };

  struct DisplayEngineData {
    // The hardware layers we've created to use on this display.
    std::vector<uint64_t> layers;

    // The number of vmos we are using in the case of software composition
    // (1 for each render target).
    uint32_t vmo_count = 0;

    // The current target that is being rendererd to by the software renderer.
    uint32_t curr_vmo = 0;

    // The information used to create images for each render target from the vmo data.
    std::vector<allocation::ImageMetadata> targets;

    // Used to synchronize buffer rendering with setting the buffer on the display.
    std::vector<FrameEventData> frame_event_datas;
  };

  // Generates a new FrameEventData struct to be used with a render target on a display.
  FrameEventData NewFrameEventData();

  // Generates a hardware layer for direct compositing on the display. Returns the ID used
  // to reference that layer in the display controller API.
  uint64_t CreateDisplayLayer();

  // Does all the setup for applying the render data, which includes images and rectangles,
  // onto the display via the display controller interface. Returns false if this cannot
  // be completed.
  bool SetRenderDataOnDisplay(const RenderData& data);

  // Sets the provided layers onto the display referenced by the given display_id.
  void SetDisplayLayers(uint64_t display_id, const std::vector<uint64_t>& layers);

  // Takes an image and directly composites it to a hardware layer on the display.
  void ApplyLayerImage(uint32_t layer_id, escher::Rectangle2D rectangle,
                       allocation::ImageMetadata image, scenic_impl::DisplayEventId wait_id,
                       scenic_impl::DisplayEventId signal_id);

  // Checks if the display controller is capable of applying the configuration settings that
  // have been set up until that point
  DisplayConfigResponse CheckConfig();

  // Erases the configuration that has been set on the display controller.
  void DiscardConfig();

  // Applies the config to the display controller. This should only be called after CheckConfig
  // has verified that the config is okay, since ApplyConfig does not return any errors.
  void ApplyConfig();

  // Returns the image id used by the display controller.
  uint64_t InternalImageId(allocation::GlobalImageId image_id) const;

  // This mutex protects access to |display_controller_| and |image_id_map_|.
  //
  // TODO(fxbug.dev/44335): Convert this to a lock-free structure. This is a unique
  // case since we are talking to a FIDL interface (display_controller_) through a lock.
  // We either need lock-free threadsafe FIDL bindings, multiple channels to the display
  // controller, or something else.
  mutable std::mutex lock_;

  // Handle to the display controller interface.
  std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller_;

  // Maps the flatland global image id to the image id used by the display controller.
  std::unordered_map<allocation::GlobalImageId, uint64_t> image_id_map_;

  // Software renderer used when render data cannot be directly composited to the display.
  const std::shared_ptr<Renderer> renderer_;

  // Maps a display ID to the the DisplayInfo struct. This is kept separate from the
  // display_DisplayCompositor_data_map_ since this only this data is needed for the
  // render_data_func_.
  std::unordered_map<uint64_t, DisplayInfo> display_info_map_;

  // Maps a display ID to a struct of all the information needed to properly render to
  // that display in both the hardware and software composition paths.
  std::unordered_map<uint64_t, DisplayEngineData> display_engine_data_map_;

  // Maps a buffer collection ID to a BufferCollectionSyncPtr. This is used as a bridge between
  // ImportBufferCollection() and ImportBufferImage() calls, so that we can check if the display
  // token can be used on the existing allocation.
  std::unordered_map<allocation::GlobalBufferCollectionId, fuchsia::sysmem::BufferCollectionSyncPtr>
      display_tokens_;

  // Maps a buffer collection ID to a boolean indicating if it can be imported into display.
  std::unordered_map<allocation::GlobalBufferCollectionId, bool>
      buffer_collection_supports_display_;

  ReleaseFenceManager release_fence_manager_;

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;

  // See BufferCollectionImportMode definition for what each mode means. by default, we add display
  // constraints as AttachTokens.
  BufferCollectionImportMode import_mode_ = BufferCollectionImportMode::AttemptDisplayConstraints;

  // TODO(fxbug.dev/77414): we use a weak ptr to safely post a task that might outlive this
  // DisplayCompositor, see RenderFrame().  This task simulates a vsync callback that we aren't yet
  // receiving because the display controller doesn't yet implement the ApplyConfig2() method. It's
  // likely that this weak_factory_ will become unnecessary at that time.
  fxl::WeakPtrFactory<DisplayCompositor> weak_factory_;  // must be last
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_DISPLAY_COMPOSITOR_H_
