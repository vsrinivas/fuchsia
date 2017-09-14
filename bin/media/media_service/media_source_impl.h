// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "garnet/bin/media/fidl/fidl_conversion_pipeline_builder.h"
#include "garnet/bin/media/fidl/fidl_packet_producer.h"
#include "garnet/bin/media/framework/types/stream_type.h"
#include "garnet/bin/media/media_service/media_service_impl.h"
#include "garnet/bin/media/util/fidl_publisher.h"
#include "garnet/bin/media/util/incident.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/media/fidl/logs/media_source_channel.fidl.h"
#include "lib/media/fidl/media_source.fidl.h"
#include "lib/media/fidl/seeking_reader.fidl.h"
#include "lib/media/flog/flog.h"

namespace media {

// Fidl agent that produces streams from an origin specified by URL.
class MediaSourceImpl : public MediaServiceImpl::Product<MediaSource>,
                        public MediaSource {
 public:
  static std::shared_ptr<MediaSourceImpl> Create(
      fidl::InterfaceHandle<SeekingReader> reader,
      const fidl::Array<MediaTypeSetPtr>& allowed_media_types,
      fidl::InterfaceRequest<MediaSource> request,
      MediaServiceImpl* owner);

  ~MediaSourceImpl() override;

  // MediaSource implementation.
  void Describe(const DescribeCallback& callback) override;

  void GetPacketProducer(
      uint32_t stream_index,
      fidl::InterfaceRequest<MediaPacketProducer> request) override;

  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void Flush(bool hold_frame, const FlushCallback& callback) override;

  void Seek(int64_t position, const SeekCallback& callback) override;

 private:
  MediaSourceImpl(fidl::InterfaceHandle<SeekingReader> reader,
                  const fidl::Array<MediaTypeSetPtr>& allowed_media_types,
                  fidl::InterfaceRequest<MediaSource> request,
                  MediaServiceImpl* owner);

  class Stream {
   public:
    Stream(size_t stream_index,
#ifdef FLOG_ENABLED
           flog::FlogProxy<logs::MediaSourceChannel>* log_channel,
#endif
           const MediaServicePtr& media_service,
           const ProducerGetter& producer_getter,
           std::unique_ptr<StreamType> stream_type,
           const std::unique_ptr<std::vector<std::unique_ptr<StreamTypeSet>>>&
               allowed_stream_types,
           const std::function<void()>& callback);

    ~Stream();

    // Gets the media type of the stream.
    MediaTypePtr media_type() const;

    // Gets the producer.
    void GetPacketProducer(fidl::InterfaceRequest<MediaPacketProducer> request);

    bool valid() { return !!producer_getter_; }

    ProducerGetter producer_getter_;
    std::unique_ptr<StreamType> stream_type_;
  };

  // Handles a status update from the demux. When called with the default
  // argument values, initiates demux status updates.
  void HandleDemuxStatusUpdates(uint64_t version = MediaSource::kInitialStatus,
                                MediaSourceStatusPtr status = nullptr);

  std::unique_ptr<std::vector<std::unique_ptr<StreamTypeSet>>>
      allowed_stream_types_;
  MediaServicePtr media_service_;
  MediaSourcePtr demux_;
  Incident init_complete_;
  std::vector<std::unique_ptr<Stream>> streams_;
  MediaSourceStatusPtr demux_status_;
  FidlPublisher<GetStatusCallback> status_publisher_;

  FLOG_INSTANCE_CHANNEL(logs::MediaSourceChannel, log_channel_);
  FXL_DISALLOW_COPY_AND_ASSIGN(MediaSourceImpl);
};

}  // namespace media
