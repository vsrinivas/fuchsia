// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_FIDL_VIDEO_RENDERER_H_
#define GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_FIDL_VIDEO_RENDERER_H_

#include <deque>
#include <unordered_map>

#include <fuchsia/media/cpp/fidl.h>

#include "garnet/bin/media/media_player/metrics/packet_timing_tracker.h"
#include "garnet/bin/media/media_player/metrics/rate_tracker.h"
#include "garnet/bin/media/media_player/metrics/value_tracker.h"
#include "garnet/bin/media/media_player/render/video_converter.h"
#include "garnet/bin/media/media_player/render/video_renderer.h"
#include "lib/ui/scenic/client/host_image_cycler.h"
#include "lib/ui/view_framework/base_view.h"

namespace media_player {

// AudioRenderer that renders video via FIDL services.
class FidlVideoRenderer
    : public VideoRendererInProc,
      public std::enable_shared_from_this<FidlVideoRenderer> {
 public:
  static std::shared_ptr<FidlVideoRenderer> Create();

  FidlVideoRenderer();

  ~FidlVideoRenderer() override;

  // VideoRendererInProc implementation.
  const char* label() const override;

  void Dump(std::ostream& os) const override;

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
  void CreateView(
      fidl::InterfacePtr<::fuchsia::ui::views_v1::ViewManager> view_manager,
      fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request);

 protected:
  void OnProgressStarted() override;

 private:
  static constexpr uint32_t kPacketDemand = 3;

  class View : public mozart::BaseView {
   public:
    View(::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
         fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner>
             view_owner_request,
         std::shared_ptr<FidlVideoRenderer> renderer);

    ~View() override;

   private:
    // |BaseView|:
    void OnSceneInvalidated(
        fuchsia::images::PresentationInfo presentation_info) override;

    std::shared_ptr<FidlVideoRenderer> renderer_;
    scenic::HostImageCycler image_cycler_;

    FXL_DISALLOW_COPY_AND_ASSIGN(View);
  };

  // Advances reference time to the indicated value. This ensures that
  // |GetSize| and |GetRgbaFrame| refer to the video frame appropriate to
  // the specified reference time and that obsolete packets are discarded.
  void AdvanceReferenceTime(int64_t reference_time);

  void GetRgbaFrame(uint8_t* rgba_buffer,
                    const fuchsia::math::Size& rgba_buffer_size);

  // Discards packets that are older than pts_ns_.
  void DiscardOldPackets();

  // Checks |packet| for a revised stream type and updates state accordingly.
  void CheckForRevisedStreamType(const PacketPtr& packet);

  // Calls Invalidate on all registered views.
  void InvalidateViews();

  // Called when a view's |OnSceneInvalidated| is called.
  void OnSceneInvalidated(int64_t reference_time);

  // Determines whether we need more packets.
  bool need_more_packets() const {
    return !flushed_ && !end_of_stream_pending() &&
           (packet_queue_.size() + (held_packet_ ? 1 : 0) < kPacketDemand);
  }

  std::vector<std::unique_ptr<StreamTypeSet>> supported_stream_types_;
  std::deque<PacketPtr> packet_queue_;
  bool flushed_ = true;
  PacketPtr held_packet_;
  int64_t pts_ns_ = Packet::kUnknownPts;
  VideoConverter converter_;
  std::unordered_map<View*, std::unique_ptr<View>> views_;
  fit::closure prime_callback_;
  fit::closure geometry_update_callback_;

  PacketTimingTracker arrivals_;
  PacketTimingTracker draws_;
  RateTracker frame_rate_;
  ValueTracker<int64_t> scenic_lead_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FidlVideoRenderer);
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIA_MEDIA_PLAYER_RENDER_FIDL_VIDEO_RENDERER_H_
