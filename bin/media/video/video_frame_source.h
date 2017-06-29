// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>
#include <unordered_set>

#include "apps/media/lib/flog/flog.h"
#include "apps/media/lib/timeline/timeline_function.h"
#include "apps/media/lib/transport/media_packet_consumer_base.h"
#include "apps/media/services/logs/media_renderer_channel.fidl.h"
#include "apps/media/services/media_renderer.fidl.h"
#include "apps/media/services/media_transport.fidl.h"
#include "apps/media/services/video_renderer.fidl.h"
#include "apps/media/src/util/fidl_publisher.h"
#include "apps/media/src/util/timeline_control_point.h"
#include "apps/media/src/video/video_converter.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/geometry/geometry.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace media {

// Implements MediaRenderer for an app that wants to show video.
class VideoFrameSource : public MediaPacketConsumerBase, public MediaRenderer {
 public:
  VideoFrameSource();

  ~VideoFrameSource() override;

  void Bind(fidl::InterfaceRequest<MediaRenderer> media_renderer_request);

  void RegisterView(mozart::BaseView* view) { views_.insert(view); }

  void UnregisterView(mozart::BaseView* view) { views_.erase(view); }

  // Advances reference time to the indicated value. This ensures that
  // |GetSize| and |GetRgbaFrame| refer to the video frame appropriate to
  // the specified reference time.
  void AdvanceReferenceTime(int64_t reference_time);

  // Returns the current video size.
  mozart::Size GetSize() { return converter_.GetSize(); }

  // Determines if views should animate because presentation time is
  // progressing.
  bool views_should_animate() { return timeline_control_point_.Progressing(); }

  // Gets status (see |VideoRenderer::GetStatus|).
  void GetStatus(uint64_t version_last_seen,
                 const VideoRenderer::GetStatusCallback& callback);

  // Gets an RGBA video frame corresponding to the current reference time.
  void GetRgbaFrame(uint8_t* rgba_buffer, const mozart::Size& rgba_buffer_size);

 private:
  static constexpr uint32_t kPacketDemand = 3;

  // MediaRenderer implementation.
  void GetSupportedMediaTypes(
      const GetSupportedMediaTypesCallback& callback) override;

  void SetMediaType(MediaTypePtr media_type) override;

  void GetPacketConsumer(fidl::InterfaceRequest<MediaPacketConsumer>
                             packet_consumer_request) override;

  void GetTimelineControlPoint(fidl::InterfaceRequest<MediaTimelineControlPoint>
                                   control_point_request) override;

  // MediaPacketConsumerBase overrides.
  void OnPacketSupplied(
      std::unique_ptr<SuppliedPacket> supplied_packet) override;

  void OnFlushRequested(bool hold_frame,
                        const FlushCallback& callback) override;

  void OnFailure() override;

  // Returns the supported media types.
  fidl::Array<MediaTypeSetPtr> SupportedMediaTypes();

  // Discards packets that are older than pts_.
  void DiscardOldPackets();

  // Checks |packet| for a revised media type and updates state accordingly.
  void CheckForRevisedMediaType(const MediaPacketPtr& packet);

  // Calls Invalidate on all registered views.
  void InvalidateViews() {
    for (mozart::BaseView* view : views_) {
      view->Invalidate();
    }
  }

  fidl::Binding<MediaRenderer> media_renderer_binding_;
  std::queue<std::unique_ptr<SuppliedPacket>> packet_queue_;
  std::unique_ptr<SuppliedPacket> held_packet_;
  TimelineFunction current_timeline_function_;
  int64_t pts_ = kUnspecifiedTime;
  int64_t min_pts_ = kMinTime;
  VideoConverter converter_;
  std::unordered_set<mozart::BaseView*> views_;
  FidlPublisher<VideoRenderer::GetStatusCallback> status_publisher_;
  MediaTimelineControlPoint::PrimeCallback prime_callback_;
  TimelineControlPoint timeline_control_point_;

  // We don't use FLOG_INSTANCE_CHANNEL, because we don't need to know the
  // address (this), and the consumer (our base class) will register with that
  // same address.
  FLOG_CHANNEL(logs::MediaRendererChannel, log_channel_);
};

}  // namespace media
