// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "apps/media/lib/flog/flog.h"
#include "apps/media/services/logs/media_demux_channel.fidl.h"
#include "apps/media/services/media_source.fidl.h"
#include "apps/media/services/seeking_reader.fidl.h"
#include "apps/media/src/demux/demux.h"
#include "apps/media/src/fidl/fidl_packet_producer.h"
#include "apps/media/src/framework/graph.h"
#include "apps/media/src/media_service/media_service_impl.h"
#include "apps/media/src/util/fidl_publisher.h"
#include "apps/media/src/util/incident.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/tasks/task_runner.h"

namespace media {

// Fidl agent that decodes a stream.
class MediaDemuxImpl : public MediaServiceImpl::Product<MediaSource>,
                       public MediaSource {
 public:
  static std::shared_ptr<MediaDemuxImpl> Create(
      fidl::InterfaceHandle<SeekingReader> reader,
      fidl::InterfaceRequest<MediaSource> request,
      MediaServiceImpl* owner);

  ~MediaDemuxImpl() override;

  // MediaSource implementation.
  void Describe(const DescribeCallback& callback) override;

  void GetPacketProducer(
      uint32_t stream_index,
      fidl::InterfaceRequest<MediaPacketProducer> producer) override;

  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void Flush(const FlushCallback& callback) override;

  void Seek(int64_t position, const SeekCallback& callback) override;

 private:
  MediaDemuxImpl(fidl::InterfaceHandle<SeekingReader> reader,
                 fidl::InterfaceRequest<MediaSource> request,
                 MediaServiceImpl* owner);

  class Stream {
   public:
    Stream(OutputRef output,
           std::unique_ptr<StreamType> stream_type,
           Graph* graph);

    ~Stream();

    // Gets the media type of the stream.
    MediaTypePtr media_type() const;

    // Returns the stream's producer.
    std::shared_ptr<FidlPacketProducer> producer() const { return producer_; }

    // Binds the producer.
    void BindPacketProducer(
        fidl::InterfaceRequest<MediaPacketProducer> producer);

    // Tells the producer to flush its connection.
    void FlushConnection(
        const FidlPacketProducer::FlushConnectionCallback callback);

   private:
    std::unique_ptr<StreamType> stream_type_;
    Graph* graph_;
    OutputRef output_;
    std::shared_ptr<FidlPacketProducer> producer_;
  };

  // Handles the completion of demux initialization.
  void OnDemuxInitialized(Result result);

  // Reports a problem via status.
  void ReportProblem(const std::string& type, const std::string& details);

  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  Graph graph_;
  PartRef demux_part_;
  std::shared_ptr<Demux> demux_;
  Incident init_complete_;
  std::vector<std::unique_ptr<Stream>> streams_;
  FidlPublisher<GetStatusCallback> status_publisher_;
  MediaMetadataPtr metadata_;
  ProblemPtr problem_;

  FLOG_INSTANCE_CHANNEL(logs::MediaDemuxChannel, log_channel_);
};

}  // namespace media
