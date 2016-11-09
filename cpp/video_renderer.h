// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>
#include <queue>

#include "apps/media/cpp/media_packet_consumer_base.h"
#include "apps/media/cpp/timeline_function.h"
#include "apps/media/cpp/video_converter.h"
#include "apps/media/interfaces/media_renderer.fidl.h"
#include "apps/media/interfaces/media_transport.fidl.h"
#include "apps/mozart/services/geometry/geometry.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace media {

// Implements MediaRenderer for an app that wants to show video.
class VideoRenderer : public MediaPacketConsumerBase,
                      public MediaRenderer,
                      public MediaTimelineControlPoint,
                      public TimelineConsumer {
 public:
  VideoRenderer();

  ~VideoRenderer() override;

  void Bind(fidl::InterfaceRequest<MediaRenderer> renderer_request);

  // Get the size of the video to be rendered.
  mozart::Size GetSize();

  // Gets an RGBA video frame corresponding to the specified reference time.
  void GetRgbaFrame(uint8_t* rgba_buffer,
                    const mozart::Size& rgba_buffer_size,
                    int64_t reference_time);

 private:
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

  void OnFlushRequested(const FlushCallback& callback) override;

  void OnFailure() override;

  // MediaTimelineControlPoint implementation.
  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void GetTimelineConsumer(fidl::InterfaceRequest<TimelineConsumer>
                               timeline_consumer_request) override;

  void Prime(const PrimeCallback& callback) override;

  // TimelineConsumer implementation.
  void SetTimelineTransform(
      TimelineTransformPtr timeline_transform,
      const SetTimelineTransformCallback& callback) override;

  // Discards packets that are older than pts_.
  void DiscardOldPackets();

  // Clears the pending timeline function and calls its associated callback
  // with the indicated completed status.
  void ClearPendingTimelineFunction(bool completed);

  // Apply a pending timeline change if there is one an it's due.
  void MaybeApplyPendingTimelineChange(int64_t reference_time);

  // Clears end-of-stream if it's set.
  void MaybeClearEndOfStream();

  // Publishes end-of-stream as needed.
  void MaybePublishEndOfStream();

  // Sends status updates to waiting callers of GetStatus.
  void SendStatusUpdates();

  // Calls the callback with the current status.
  void CompleteGetStatus(const GetStatusCallback& callback);

  fidl::Binding<MediaRenderer> renderer_binding_;
  fidl::Binding<MediaTimelineControlPoint> control_point_binding_;
  fidl::Binding<TimelineConsumer> timeline_consumer_binding_;
  std::queue<std::unique_ptr<SuppliedPacket>> packet_queue_;
  TimelineFunction current_timeline_function_;
  TimelineFunction pending_timeline_function_;
  SetTimelineTransformCallback set_timeline_transform_callback_;
  int64_t pts_ = kUnspecifiedTime;
  int64_t end_of_stream_pts_ = kUnspecifiedTime;
  bool end_of_stream_published_ = false;
  uint64_t status_version_ = 1u;
  std::vector<GetStatusCallback> pending_status_callbacks_;
  VideoConverter converter_;
};

}  // namespace media
