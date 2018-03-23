// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>
#include <unordered_set>

#include <fuchsia/cpp/geometry.h>
#include <fuchsia/cpp/media.h>
#include "garnet/bin/media/util/timeline_control_point.h"
#include "garnet/bin/media/video/video_converter.h"
#include "lib/media/timeline/timeline_function.h"
#include "lib/media/transport/media_packet_consumer_base.h"
#include "lib/ui/view_framework/base_view.h"

namespace media {

// Implements MediaPacketConsumer for an app that wants to show video.
class VideoFrameSource : public MediaPacketConsumerBase {
 public:
  VideoFrameSource();

  ~VideoFrameSource() override;

  // Sets a callback that's called when the sink adopts a revised stream type
  // carried on a packet.
  void SetStreamTypeRevisedCallback(const fxl::Closure& callback) {
    stream_type_revised_callback_ = callback;
  }

  // Binds the packet consumer.
  void BindConsumer(f1dl::InterfaceRequest<MediaPacketConsumer> request) {
    MediaPacketConsumerBase::Bind(std::move(request));
  }

  // Binds the timeline control point.
  void BindTimelineControlPoint(
      f1dl::InterfaceRequest<MediaTimelineControlPoint> request) {
    timeline_control_point_.Bind(std::move(request));
  }

  // Gets the video converter.
  VideoConverter& converter() { return converter_; }

  // Adds a view.
  void AddView(mozart::BaseView* view) { views_.insert(view); }

  // Removes a view.
  void RemoveView(mozart::BaseView* view) { views_.erase(view); }

  // Removes all views.
  void RemoveAllViews() { views_.clear(); }

  // Advances reference time to the indicated value. This ensures that
  // |GetSize| and |GetRgbaFrame| refer to the video frame appropriate to
  // the specified reference time.
  void AdvanceReferenceTime(int64_t reference_time);

  // Determines if views should animate because presentation time is
  // progressing.
  bool views_should_animate() { return timeline_control_point_.Progressing(); }

  // Gets status (see |VideoRenderer::GetStatus|).
  void GetStatus(uint64_t version_last_seen,
                 const VideoRenderer::GetStatusCallback& callback);

  // Gets an RGBA video frame corresponding to the current reference time.
  void GetRgbaFrame(uint8_t* rgba_buffer,
                    const geometry::Size& rgba_buffer_size);

 private:
  static constexpr uint32_t kPacketDemand = 3;

  // MediaPacketConsumerBase overrides.
  void OnPacketSupplied(
      std::unique_ptr<SuppliedPacket> supplied_packet) override;

  void OnFlushRequested(bool hold_frame, FlushCallback callback) override;

  void OnFailure() override;

  // Returns the supported media types.
  f1dl::VectorPtr<MediaTypeSetPtr> SupportedMediaTypes();

  // Discards packets that are older than pts_.
  void DiscardOldPackets();

  // Checks |packet| for a revised media type and updates state accordingly.
  void CheckForRevisedMediaType(const MediaPacketPtr& packet);

  // Calls Invalidate on all registered views.
  void InvalidateViews() {
    for (mozart::BaseView* view : views_) {
      view->InvalidateScene();
    }
  }

  std::queue<std::unique_ptr<SuppliedPacket>> packet_queue_;
  std::unique_ptr<SuppliedPacket> held_packet_;
  TimelineFunction current_timeline_function_;
  int64_t pts_ = kUnspecifiedTime;
  int64_t min_pts_ = kMinTime;
  VideoConverter converter_;
  std::unordered_set<mozart::BaseView*> views_;
  MediaTimelineControlPoint::PrimeCallback prime_callback_;
  fxl::Closure stream_type_revised_callback_;
  TimelineControlPoint timeline_control_point_;
};

}  // namespace media
