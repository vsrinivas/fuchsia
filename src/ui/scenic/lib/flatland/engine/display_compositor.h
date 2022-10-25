// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_DISPLAY_COMPOSITOR_H_
#define SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_DISPLAY_COMPOSITOR_H_

#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async/dispatcher.h>

#include <deque>
#include <memory>
#include <unordered_map>

#include "src/lib/fxl/memory/weak_ptr.h"
#include "src/ui/scenic/lib/allocation/buffer_collection_importer.h"
#include "src/ui/scenic/lib/display/display.h"
#include "src/ui/scenic/lib/display/util.h"
#include "src/ui/scenic/lib/flatland/engine/engine_types.h"
#include "src/ui/scenic/lib/flatland/engine/release_fence_manager.h"

namespace flatland {

namespace test {
class DisplayCompositorSmokeTest;
class DisplayCompositorPixelTest;
class DisplayCompositorTest;
}  // namespace test

using allocation::BufferCollectionUsage;

// The DisplayCompositor is responsible for compositing Flatland render data onto the display(s).
// It accomplishes this either by direct hardware compositing via the display controller
// interface, or rendering on the GPU via a custom renderer API. It also handles the
// registration of sysmem buffer collections and importation of images to both the
// display controller and the renderer via the BufferCollectionImporter interface. The
// BufferCollectionImporter interface is how Flatland instances communicate with the
// DisplayCompositor, providing it with the necessary data to render without exposing to Flatland
// the DisplayController or other dependencies.
class DisplayCompositor final : public allocation::BufferCollectionImporter,
                                public std::enable_shared_from_this<DisplayCompositor> {
 public:
#ifdef DISABLE_DISPLAY_COMPOSITION
  // Uses the GPU/Vulkan compositor by default, instead of attempting to composit using the display
  // controller.
  constexpr static bool kDisableDisplayComposition = true;
#else
  constexpr static bool kDisableDisplayComposition = false;
#endif

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
  bool ImportBufferCollection(allocation::GlobalBufferCollectionId collection_id,
                              fuchsia::sysmem::Allocator_Sync* sysmem_allocator,
                              fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                              BufferCollectionUsage usage,
                              std::optional<fuchsia::math::SizeU> size) override;

  // |BufferCollectionImporter|
  void ReleaseBufferCollection(allocation::GlobalBufferCollectionId collection_id,
                               BufferCollectionUsage usage_type) override;

  // |BufferCollectionImporter|
  bool ImportBufferImage(const allocation::ImageMetadata& metadata,
                         BufferCollectionUsage usage_type) override;

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
  // presented on the display when compositing on the GPU. If |num_render_targets| is 0, this
  // function will not create any render targets for GPU composition for that display. The buffer
  // collection info is also returned back to the caller via an output parameter
  // |out_collection_info|. This out parameter is only allowed to be nullptr when
  // |num_render_targets| is 0. Otherwise, a valid handle to return the buffer collection data is
  // required.
  // TODO(fxbug.dev/59646): We need to figure out exactly how we want the display to anchor
  // to the Flatland hierarchy.
  void AddDisplay(scenic_impl::display::Display* display, DisplayInfo info,
                  uint32_t num_render_targets,
                  fuchsia::sysmem::BufferCollectionInfo_2* out_collection_info);

  // Values needed to adjust the color of the framebuffer as a postprocessing effect.
  void SetColorConversionValues(const std::array<float, 9>& coefficients,
                                const std::array<float, 3>& preoffsets,
                                const std::array<float, 3>& postoffsets);

  // Clamps the minimum value for all channels on all pixels on the display to this number.
  bool SetMinimumRgb(uint8_t minimum_rgb);

 private:
  friend class test::DisplayCompositorSmokeTest;
  friend class test::DisplayCompositorPixelTest;
  friend class test::DisplayCompositorTest;

  // Notifies the compositor that a vsync has occurred, in response to a display configuration
  // applied by the compositor.  It is the compositor's responsibility to signal any release fences
  // corresponding to the frame identified by |frame_number|.
  void OnVsync(zx::time timestamp, fuchsia::hardware::display::ConfigStamp applied_config_stamp);

  std::vector<allocation::ImageMetadata> AllocateDisplayRenderTargets(
      bool use_protected_memory, uint32_t num_render_targets, const fuchsia::math::SizeU& size,
      zx_pixel_format_t pixel_format, fuchsia::sysmem::BufferCollectionInfo_2* out_collection_info);

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

  struct ImageEventData {
    scenic_impl::DisplayEventId signal_id;
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
    std::vector<allocation::ImageMetadata> render_targets;

    // The information used to create images for each render target from the vmo data.
    std::vector<allocation::ImageMetadata> protected_render_targets;

    // Used to synchronize buffer rendering with setting the buffer on the display.
    std::vector<FrameEventData> frame_event_datas;
  };

  // Generates a new FrameEventData struct to be used with a render target on a display.
  FrameEventData NewFrameEventData();

  // Generates a new ImageEventData struct to be used with a client image on a display.
  ImageEventData NewImageEventData();

  // Generates a hardware layer for direct compositing on the display. Returns the ID used
  // to reference that layer in the display controller API.
  uint64_t CreateDisplayLayer();

  // Does all the setup for applying the render data, which includes images and rectangles,
  // onto the display via the display controller interface. Returns false if this cannot
  // be completed.
  bool SetRenderDataOnDisplay(const RenderData& data);

  // Sets the provided layers onto the display referenced by the given display_id.
  void SetDisplayLayers(uint64_t display_id, const std::vector<uint64_t>& layers);

  // Takes a solid color rectangle and directly composites it to a hardware layer on the display.
  void ApplyLayerColor(uint32_t layer_id, ImageRect rectangle, allocation::ImageMetadata image);

  // Takes an image and directly composites it to a hardware layer on the display.
  void ApplyLayerImage(uint32_t layer_id, ImageRect rectangle, allocation::ImageMetadata image,
                       scenic_impl::DisplayEventId wait_id, scenic_impl::DisplayEventId signal_id);

  // Checks if the display controller is capable of applying the configuration settings that
  // have been set up until that point
  DisplayConfigResponse CheckConfig();

  // Erases the configuration that has been set on the display controller.
  void DiscardConfig();

  // Applies the config to the display controller and returns the ConfigStamp associated with this
  // config. ConfigStamp is provided by the display controller. This should only be called after
  // CheckConfig has verified that the config is okay, since ApplyConfig does not return any errors.
  fuchsia::hardware::display::ConfigStamp ApplyConfig();

  // This mutex protects access to |display_controller_| and |image_id_map_|.
  //
  // TODO(fxbug.dev/44335): Convert this to a lock-free structure. This is a unique
  // case since we are talking to a FIDL interface (display_controller_) through a lock.
  // We either need lock-free threadsafe FIDL bindings, multiple channels to the display
  // controller, or something else.
  mutable std::mutex lock_;

  // Handle to the display controller interface.
  std::shared_ptr<fuchsia::hardware::display::ControllerSyncPtr> display_controller_;

  // Maps the flatland global image id to the events used by the display controller.
  std::unordered_map<allocation::GlobalImageId, ImageEventData> image_event_map_;

  // Pending images in the current config that hasn't been applied yet.
  std::vector<allocation::GlobalImageId> pending_images_in_config_;

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

  // Maps a buffer collection ID to a collection pixel format struct.
  std::unordered_map<allocation::GlobalBufferCollectionId, fuchsia::sysmem::PixelFormat>
      buffer_collection_pixel_format_;

  ReleaseFenceManager release_fence_manager_;

  // Stores information about the last ApplyConfig() call to display.
  struct ApplyConfigInfo {
    fuchsia::hardware::display::ConfigStamp config_stamp;
    uint64_t frame_number;
  };

  // A queue storing all display frame configurations that are applied but not yet shown on the
  // display device.
  std::deque<ApplyConfigInfo> pending_apply_configs_;

  // Stores the ConfigStamp information of the latest frame shown on the display. If no frame
  // has been presented, its value will be nullopt.
  std::optional<fuchsia::hardware::display::ConfigStamp> last_presented_config_stamp_ =
      std::nullopt;

  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;

  // See BufferCollectionImportMode definition for what each mode means. by default, we add display
  // constraints as AttachTokens.
  BufferCollectionImportMode import_mode_ = BufferCollectionImportMode::AttemptDisplayConstraints;

  constexpr static std::array<float, 9> kDefaultColorConversionCoefficients = {1, 0, 0, 0, 1,
                                                                               0, 0, 0, 1};
  constexpr static std::array<float, 3> kDefaultColorConversionOffsets = {0, 0, 0};

  bool should_apply_display_color_conversion_ = false;
  std::array<float, 9> color_conversion_coefficients_ = kDefaultColorConversionCoefficients;
  std::array<float, 3> color_conversion_preoffsets_ = kDefaultColorConversionOffsets;
  std::array<float, 3> color_conversion_postoffsets_ = kDefaultColorConversionOffsets;

  // TODO(fxbug.dev/77414): we use a weak ptr to safely post a task that might outlive this
  // DisplayCompositor, see RenderFrame().  This task simulates a vsync callback that we aren't yet
  // receiving because the display controller doesn't yet implement the ApplyConfig2() method. It's
  // likely that this weak_factory_ will become unnecessary at that time.
  fxl::WeakPtrFactory<DisplayCompositor> weak_factory_;  // must be last
};

}  // namespace flatland

#endif  // SRC_UI_SCENIC_LIB_FLATLAND_ENGINE_DISPLAY_COMPOSITOR_H_
