// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/media/services/media_source.fidl.h"
#include "apps/media/services/seeking_reader.fidl.h"
#include "apps/media/src/fidl/fidl_conversion_pipeline_builder.h"
#include "apps/media/src/fidl/fidl_packet_producer.h"
#include "apps/media/src/framework/types/stream_type.h"
#include "apps/media/src/media_service/media_service_impl.h"
#include "apps/media/src/util/fidl_publisher.h"
#include "apps/media/src/util/incident.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/tasks/task_runner.h"

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

  void Flush(const FlushCallback& callback) override;

  void Seek(int64_t position, const SeekCallback& callback) override;

 private:
  MediaSourceImpl(fidl::InterfaceHandle<SeekingReader> reader,
                  const fidl::Array<MediaTypeSetPtr>& allowed_media_types,
                  fidl::InterfaceRequest<MediaSource> request,
                  MediaServiceImpl* owner);

  class Stream {
   public:
    Stream(const MediaServicePtr& media_service,
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

  // Reports a problem via status.
  void ReportProblem(const std::string& type, const std::string& details);

  // Handles a status update from the demux. When called with the default
  // argument values, initiates demux status updates.
  void HandleDemuxStatusUpdates(uint64_t version = MediaDemux::kInitialMetadata,
                                MediaDemuxStatusPtr status = nullptr);

  std::unique_ptr<std::vector<std::unique_ptr<StreamTypeSet>>>
      allowed_stream_types_;
  MediaServicePtr media_service_;
  MediaDemuxPtr demux_;
  Incident init_complete_;
  std::vector<std::unique_ptr<Stream>> streams_;
  MediaMetadataPtr metadata_;
  ProblemPtr problem_;
  FidlPublisher<GetStatusCallback> status_publisher_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MediaSourceImpl);
};

}  // namespace media
