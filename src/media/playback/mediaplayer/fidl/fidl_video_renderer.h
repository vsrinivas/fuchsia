// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_VIDEO_RENDERER_H_
#define SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_VIDEO_RENDERER_H_

#include <fbl/array.h>
#include <fuchsia/media/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async/cpp/wait.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/scenic/cpp/resources.h>

#include <queue>
#include <unordered_map>

#include "src/media/playback/mediaplayer/metrics/packet_timing_tracker.h"
#include "src/media/playback/mediaplayer/render/video_renderer.h"

namespace media_player {

// VideoRenderer that renders video via FIDL services.
class FidlVideoRenderer : public VideoRenderer {
 public:
  static std::shared_ptr<FidlVideoRenderer> Create(
      component::StartupContext* startup_context);

  FidlVideoRenderer(component::StartupContext* startup_context);

  ~FidlVideoRenderer() override;

  // VideoRenderer implementation.
  const char* label() const override;

  void Dump(std::ostream& os) const override;

  void ConfigureConnectors() override;

  void OnInputConnectionReady(size_t input_index) override;

  void FlushInput(bool hold_frame, size_t input_index,
                  fit::closure callback) override;

  void PutInputPacket(PacketPtr packet, size_t input_index) override;

  const std::vector<std::unique_ptr<StreamTypeSet>>& GetSupportedStreamTypes()
      override {
    return supported_stream_types_;
  }

  void SetStreamType(const StreamType& stream_type) override;

  void Prime(fit::closure callback) override;

  fuchsia::math::Size video_size() const override;

  fuchsia::math::Size pixel_aspect_ratio() const override;

  // Registers a callback that's called when the values returned by |video_size|
  // or |pixel_aspect_ratio| change.
  void SetGeometryUpdateCallback(fit::closure callback);

  // Creates a view.
  void CreateView(fuchsia::ui::views::ViewToken view_token);

 protected:
  // Renderer overrides.
  void OnTimelineTransition() override;

 private:
  static constexpr uint32_t kPacketDemand = 3;

  // Used to determine when all the |ImagePipe|s have released a buffer.
  class ReleaseTracker : public fbl::RefCounted<ReleaseTracker> {
   public:
    // Constructs a |ReleaseTracker|. |packet| and |renderer| are both required.
    ReleaseTracker(PacketPtr packet,
                   std::shared_ptr<FidlVideoRenderer> renderer);

    ~ReleaseTracker();

   private:
    PacketPtr packet_;
    std::shared_ptr<FidlVideoRenderer> renderer_;
  };

  struct Image {
    Image();

    ~Image() = default;

    // Called when |release_fence_| is released.
    void WaitHandler(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                     zx_status_t status, const zx_packet_signal_t* signal);

    fbl::RefPtr<PayloadVmo> vmo_;
    uint32_t image_id_;
    // If the |ImagePipe| channel closes unexpectedly, all the |Images|
    // associated with the view are deleted, so this |release_tracker_| no
    // longer prevents the renderer from releaseing the packet.
    fbl::RefPtr<ReleaseTracker> release_tracker_;
    zx::event release_fence_;
    // |release_fence_| owns the handle that |wait_| references so it's
    // important that |wait_| be destroyed first when the destructor runs.
    // Members are destroyed bottom to top, so |wait_| must be below
    // |release_fence_|.
    async::WaitMethod<Image, &Image::WaitHandler> wait_;
  };

  class View : public scenic::BaseView {
   public:
    View(scenic::ViewContext context,
         std::shared_ptr<FidlVideoRenderer> renderer);

    ~View() override;

    // Adds the black image to the image pipe.
    void AddBlackImage(uint32_t image_id, fuchsia::images::ImageInfo image_info,
                       const zx::vmo& vmo);

    // Removes the old images from the image pipe, if images were added
    // previously, and adds new images. An image is added for each VMO in
    // |vmos|, and they are numbered starting with |image_id_base|.
    void UpdateImages(uint32_t image_id_base,
                      fuchsia::images::ImageInfo image_info,
                      uint32_t display_width, uint32_t display_height,
                      const std::vector<fbl::RefPtr<PayloadVmo>>& vmos);

    // Presents the black image using the |ImagePipe|.
    void PresentBlackImage(uint32_t image_id, uint64_t presentation_time);

    // Presents an image using the |ImagePipe|.
    void PresentImage(uint32_t buffer_index, uint64_t presentation_time,
                      fbl::RefPtr<ReleaseTracker> release_tracker,
                      async_dispatcher_t* dispatcher);

   private:
    // |scenic::BaseView|
    void OnSceneInvalidated(
        fuchsia::images::PresentationInfo presentation_info) override;

    // |scenic::SessionListener|
    void OnScenicError(std::string error) override {
      FXL_LOG(ERROR) << "Scenic Error " << error;
    }

    std::shared_ptr<FidlVideoRenderer> renderer_;

    scenic::EntityNode entity_node_;
    scenic::ShapeNode clip_node_;
    scenic::ShapeNode image_pipe_node_;
    scenic::Material image_pipe_material_;

    fuchsia::images::ImagePipePtr image_pipe_;

    uint32_t image_width_;
    uint32_t image_height_;
    uint32_t display_width_;
    uint32_t display_height_;
    fbl::Array<Image> images_;

    // Disallow copy, assign and move.
    View(const View&) = delete;
    View(View&&) = delete;
    View& operator=(const View&) = delete;
    View& operator=(View&&) = delete;
  };

  // Updates the images added to the image pipes associated with the views.
  void UpdateImages();

  // Presents a black image immediately.
  void PresentBlackImage();

  // Present |packet| at |scenic_presentation_time|.
  void PresentPacket(PacketPtr packet, int64_t scenic_presentation_time);

  // Called when all image pipes have released an image that was submitted for
  // presentation.
  void PacketReleased(PacketPtr packet);

  // Completes a pending flush if all packets (except maybe the held frame) are
  // released.
  void MaybeCompleteFlush();

  // Checks |packet| for a revised stream type and updates state accordingly.
  void CheckForRevisedStreamType(const PacketPtr& packet);

  // Determines whether we need more packets.
  bool need_more_packets() const {
    return !flushed_ && !end_of_stream_pending() &&
           (presented_packets_not_released_ +
            packets_awaiting_presentation_.size()) < kPacketDemand;
  }

  bool have_valid_image_info() {
    return image_info_.width != 0 && image_info_.height != 0;
  }

  component::StartupContext* startup_context_;
  fidl::InterfacePtr<fuchsia::ui::scenic::Scenic> scenic_;

  std::vector<std::unique_ptr<StreamTypeSet>> supported_stream_types_;
  bool input_connection_ready_ = false;
  zx::vmo black_image_vmo_;
  fuchsia::images::ImageInfo image_info_{};
  uint32_t display_width_{};
  uint32_t display_height_{};
  fuchsia::math::Size pixel_aspect_ratio_{.width = 1, .height = 1};
  uint32_t presented_packets_not_released_ = 0;
  bool flushed_ = true;
  fit::closure flush_callback_;
  bool flush_hold_frame_;
  bool initial_packet_presented_ = false;
  std::queue<PacketPtr> packets_awaiting_presentation_;
  std::unordered_map<View*, std::unique_ptr<View>> views_;
  fit::closure prime_callback_;
  fit::closure geometry_update_callback_;
  uint32_t image_id_base_ = 2;  // 1 is reserved for the black image.
  uint32_t next_image_id_base_ = 2;

  PacketTimingTracker arrivals_;

  // Disallow copy, assign and move.
  FidlVideoRenderer(const FidlVideoRenderer&) = delete;
  FidlVideoRenderer(FidlVideoRenderer&&) = delete;
  FidlVideoRenderer& operator=(const FidlVideoRenderer&) = delete;
  FidlVideoRenderer& operator=(FidlVideoRenderer&&) = delete;
};

}  // namespace media_player

#endif  // SRC_MEDIA_PLAYBACK_MEDIAPLAYER_FIDL_FIDL_VIDEO_RENDERER_H_
