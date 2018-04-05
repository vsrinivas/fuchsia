// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <queue>
#include <unordered_map>

#include <fuchsia/cpp/media.h>

#include "garnet/bin/media/render/video_renderer.h"
#include "garnet/bin/media/video/video_converter.h"
#include "lib/ui/scenic/client/host_image_cycler.h"
#include "lib/ui/view_framework/base_view.h"

namespace media {

// AudioRenderer that renders video via FIDL services.
class FidlVideoRenderer
    : public VideoRendererInProc,
      public std::enable_shared_from_this<FidlVideoRenderer> {
 public:
  static std::shared_ptr<FidlVideoRenderer> Create();

  FidlVideoRenderer();

  ~FidlVideoRenderer() override;

  // VideoRendererInProc implementation.
  void Flush(bool hold_frame) override;

  std::shared_ptr<PayloadAllocator> allocator() override;

  Demand SupplyPacket(PacketPtr packet) override;

  const std::vector<std::unique_ptr<StreamTypeSet>>& GetSupportedStreamTypes()
      override {
    return supported_stream_types_;
  }

  void SetStreamType(const StreamType& stream_type) override;

  void Prime(fxl::Closure callback) override;

  geometry::Size video_size() const override;

  geometry::Size pixel_aspect_ratio() const override;

  // Registers a callback that's called when the values returned by |video_size|
  // or |pixel_aspect_ratio| change.
  void SetGeometryUpdateCallback(fxl::Closure callback);

  // Creates a view.
  void CreateView(
      fidl::InterfacePtr<views_v1::ViewManager> view_manager,
      fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request);

 protected:
  void OnProgressStarted() override;

 private:
  static constexpr uint32_t kPacketDemand = 3;

  class View : public mozart::BaseView {
   public:
    View(views_v1::ViewManagerPtr view_manager,
         fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
         std::shared_ptr<FidlVideoRenderer> renderer);

    ~View() override;

   private:
    // |BaseView|:
    void OnSceneInvalidated(
        images::PresentationInfo presentation_info) override;

    std::shared_ptr<FidlVideoRenderer> renderer_;
    scenic_lib::HostImageCycler image_cycler_;

    FXL_DISALLOW_COPY_AND_ASSIGN(View);
  };

  // Advances reference time to the indicated value. This ensures that
  // |GetSize| and |GetRgbaFrame| refer to the video frame appropriate to
  // the specified reference time.
  void AdvanceReferenceTime(int64_t reference_time);

  void GetRgbaFrame(uint8_t* rgba_buffer,
                    const geometry::Size& rgba_buffer_size);

  // Discards packets that are older than pts_.
  void DiscardOldPackets();

  // Checks |packet| for a revised stream type and updates state accordingly.
  void CheckForRevisedStreamType(const PacketPtr& packet);

  // Calls Invalidate on all registered views.
  void InvalidateViews();

  // Determines whether we need more packets.
  bool need_more_packets() const {
    return !end_of_stream_pending() &&
           (packet_queue_.size() + (held_packet_ ? 1 : 0) < kPacketDemand);
  }

  std::vector<std::unique_ptr<StreamTypeSet>> supported_stream_types_;
  std::queue<PacketPtr> packet_queue_;
  PacketPtr held_packet_;
  int64_t pts_ = Packet::kUnknownPts;
  VideoConverter converter_;
  std::unordered_map<View*, std::unique_ptr<View>> views_;
  fxl::Closure prime_callback_;
  fxl::Closure geometry_update_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FidlVideoRenderer);
};

}  // namespace media
