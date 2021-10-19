// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_CAMERA_GYM_BUFFER_COLLAGE_H_
#define SRC_CAMERA_BIN_CAMERA_GYM_BUFFER_COLLAGE_H_

#include <fuchsia/math/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/result.h>
#include <lib/ui/scenic/cpp/resources.h>

#include <map>

#include "fuchsia/camera/gym/cpp/fidl.h"

namespace camera {

struct BitmapImageNode {
  BitmapImageNode(scenic::Session* session, std::string filename, uint32_t width, uint32_t height);
  BitmapImageNode() = delete;
  uint32_t width;
  uint32_t height;
  std::unique_ptr<scenic::Memory> memory;
  std::unique_ptr<scenic::Image> image;
  std::unique_ptr<scenic::Rectangle> shape;
  scenic::Material material;
  scenic::ShapeNode node;
};

struct CollectionView {
  fuchsia::sysmem::ImageFormat_2 image_format;
  fuchsia::sysmem::BufferCollectionPtr collection;
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
  fuchsia::images::ImagePipe2Ptr image_pipe;
  uint32_t image_pipe_id;
  fuchsia::math::RectF view_region;
  std::unique_ptr<scenic::Material> material;
  std::unique_ptr<scenic::Mesh> mesh;
  std::unique_ptr<scenic::ShapeNode> node;
  std::unique_ptr<scenic::Material> highlight_material;
  std::unique_ptr<scenic::Mesh> highlight_mesh;
  std::unique_ptr<scenic::ShapeNode> highlight_node;
  bool has_content = false;
  bool darkened = true;
  std::unique_ptr<BitmapImageNode> description_node;
};

// This class takes ownership of the display and presents the contents of buffer collections in a
// grid pattern. Unless otherwise noted, public methods are thread-safe and private methods must
// only be called from the loop's thread.
class BufferCollage : public fuchsia::ui::app::ViewProvider {
 public:
  static constexpr uint32_t kShowDescription    = 0x00000001;  // On/off for description
  static constexpr uint32_t kShowMagnifyBoxes   = 0x00000002;  // On/off for magnify boxes

  static constexpr uint32_t kShowStateCycleMask = 0x00000003;  // All bits used

  static constexpr uint32_t kDefaultShowState   = kShowDescription;  // Backward compatible start

  using CommandStatusHandler =
      fit::function<void(fuchsia::camera::gym::Controller_SendCommand_Result)>;

  ~BufferCollage() override;

  // Creates a new BufferCollage instance using the provided interface handles. After returning, if
  // the instance stops running, either due to an error or explicit action, |stop_callback| is
  // invoked exactly once if non-null.
  static fpromise::result<std::unique_ptr<BufferCollage>, zx_status_t> Create(
      fuchsia::ui::scenic::ScenicHandle scenic, fuchsia::sysmem::AllocatorHandle allocator,
      fuchsia::ui::policy::PresenterHandle presenter, fit::closure stop_callback = nullptr);

  // Returns the view request handler.
  fidl::InterfaceRequestHandler<fuchsia::ui::app::ViewProvider> GetHandler();

  // Registers a new buffer collection and adds it to the view, updating the layout of existing
  // collections to fit. Returns an id representing the collection. Collections are initially
  // hidden and must be made visible using PostSetCollectionVisibility.
  fpromise::promise<uint32_t> AddCollection(fuchsia::sysmem::BufferCollectionTokenHandle token,
                                            fuchsia::sysmem::ImageFormat_2 image_format,
                                            std::string description);

  // Removes the collection with the given |id| from the view and updates the layout to fill the
  // vacated space. If |id| is not a valid collection, the instance stops.
  void RemoveCollection(uint32_t id);

  // Updates the view to show the given |buffer_index| in for the given |collection_id|'s node.
  // Holds |release_fence| until the buffer is no longer needed, then closes the handle. If
  // non-null, |subregion| specifies what sub-region of the buffer to highlight.
  void PostShowBuffer(uint32_t collection_id, uint32_t buffer_index, zx::eventpair release_fence,
                      std::optional<fuchsia::math::RectF> subregion);

  // Updates the view to show or hide a collection with the given |id|.
  void PostSetCollectionVisibility(uint32_t id, bool visible);

  // Updates the view to show or hide a mute icon.
  void PostSetMuteIconVisibility(bool visible);

  // Returns the current magnify boxes mode. (Global to all streams)
  bool show_magnify_boxes() {
    return (show_state_ & kShowMagnifyBoxes) != 0;
  }

  // Returns the current description mode. (Global to all streams)
  bool show_description() {
    return (show_state_ & kShowDescription) != 0;
  }

  // Updates the magnify boxes mode. (Global to all streams)
  void set_show_magnify_boxes(bool enable) {
    if (enable) {
      show_state_ |= kShowMagnifyBoxes;
    } else {
      show_state_ &= ~kShowMagnifyBoxes;
    }
  }

  // Updates the description mode. (Global to all streams)
  void set_show_description(bool enable) {
    if (enable) {
      show_state_ |= kShowDescription;
    } else {
      show_state_ &= ~kShowDescription;
    }
  }

  // Manual mode entry points:
  void CommandSuccessNotify();
  void ExecuteCommand(fuchsia::camera::gym::Command command, CommandStatusHandler handler);
  void PostedExecuteCommand(fuchsia::camera::gym::Command command, CommandStatusHandler handler);
  void ExecuteSetDescriptionCommand(fuchsia::camera::gym::SetDescriptionCommand& command);

 private:
  BufferCollage();

  // Requests a new view.
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::ui::app::ViewProvider> request);

  // Disconnects all channels, quits the loop, and calls the stop callback.
  void Stop();

  // Registers the provider interface's error handler to invoke Stop.
  template <typename T>
  void SetStopOnError(fidl::InterfacePtr<T>& p, std::string name = T::Name_);

  // See PostShowBuffer.
  void ShowBuffer(uint32_t collection_id, uint32_t buffer_index, zx::eventpair release_fence,
                  std::optional<fuchsia::math::RectF> subregion);

  // Repositions scenic nodes to fit all collections on the screen.
  void UpdateLayout();

  // If the host environment has not yet requested a view via the ViewProvider service, takes over
  // the display and calls CreateView using a manually created view token.
  void MaybeTakeDisplay();

  // Initializes view-dependent state after the parent scenic::View has been created.
  void SetupView();

  // |scenic::Session| callbacks.
  void OnScenicError(zx_status_t status);
  void OnScenicEvent(std::vector<fuchsia::ui::scenic::Event> events);

  // |fuchsia::ui::app::ViewProvider|
  void CreateView(zx::eventpair view_token,
                  fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> incoming_services,
                  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> outgoing_services) override;
  void CreateViewWithViewRef(zx::eventpair view_token,
                             fuchsia::ui::views::ViewRefControl view_ref_control,
                             fuchsia::ui::views::ViewRef view_ref) override;

  async::Loop loop_;
  fuchsia::ui::scenic::ScenicPtr scenic_;
  fuchsia::sysmem::AllocatorPtr allocator_;
  fuchsia::ui::policy::PresenterPtr presenter_;
  fit::closure stop_callback_;
  std::unique_ptr<scenic::Session> session_;
  std::unique_ptr<scenic::View> view_;
  fidl::Binding<fuchsia::ui::app::ViewProvider> view_provider_binding_;
  std::optional<fuchsia::ui::gfx::BoundingBox> view_extents_;
  std::map<uint32_t, CollectionView> collection_views_;
  uint32_t next_collection_id_ = 1;
  struct {
    std::unique_ptr<scenic::Material> material;
    std::unique_ptr<scenic::Circle> shape;
    std::unique_ptr<scenic::ShapeNode> node;
  } heartbeat_indicator_;
  zx::time start_time_;
  uint32_t show_state_ = kDefaultShowState;
  bool mute_visible_ = false;
  std::unique_ptr<BitmapImageNode> mute_indicator_;

  // TODO(?????) - Is this really the ideal way to communicate status back?
  CommandStatusHandler command_status_handler_;
};

}  // namespace camera

#endif  // SRC_CAMERA_BIN_CAMERA_GYM_BUFFER_COLLAGE_H_
