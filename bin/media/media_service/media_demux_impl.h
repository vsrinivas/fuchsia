// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "apps/media/cpp/flog.h"
#include "apps/media/interfaces/logs/media_demux_channel.mojom.h"
#include "apps/media/interfaces/media_demux.mojom.h"
#include "apps/media/interfaces/seeking_reader.mojom.h"
#include "apps/media/src/demux/demux.h"
#include "apps/media/src/framework/graph.h"
#include "apps/media/src/media_service/media_service_impl.h"
#include "apps/media/src/mojo/mojo_packet_producer.h"
#include "apps/media/src/util/incident.h"
#include "apps/media/src/util/mojo_publisher.h"
#include "lib/ftl/tasks/task_runner.h"
#include "mojo/public/cpp/bindings/binding.h"

namespace mojo {
namespace media {

// Mojo agent that decodes a stream.
class MediaDemuxImpl : public MediaServiceImpl::Product<MediaDemux>,
                       public MediaDemux {
 public:
  static std::shared_ptr<MediaDemuxImpl> Create(
      InterfaceHandle<SeekingReader> reader,
      InterfaceRequest<MediaDemux> request,
      MediaServiceImpl* owner);

  ~MediaDemuxImpl() override;

  // MediaDemux implementation.
  void Describe(const DescribeCallback& callback) override;

  void GetPacketProducer(
      uint32_t stream_index,
      InterfaceRequest<MediaPacketProducer> producer) override;

  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void Flush(const FlushCallback& callback) override;

  void Seek(int64_t position, const SeekCallback& callback) override;

 private:
  MediaDemuxImpl(InterfaceHandle<SeekingReader> reader,
                 InterfaceRequest<MediaDemux> request,
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
    std::shared_ptr<MojoPacketProducer> producer() const { return producer_; }

    // Binds the producer.
    void BindPacketProducer(InterfaceRequest<MediaPacketProducer> producer);

    // Tells the producer to flush its connection.
    void FlushConnection(
        const MojoPacketProducer::FlushConnectionCallback callback);

   private:
    std::unique_ptr<StreamType> stream_type_;
    Graph* graph_;
    OutputRef output_;
    std::shared_ptr<MojoPacketProducer> producer_;
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
  MojoPublisher<GetStatusCallback> status_publisher_;
  MediaMetadataPtr metadata_;
  ProblemPtr problem_;

  FLOG_INSTANCE_CHANNEL(logs::MediaDemuxChannel, log_channel_);
};

}  // namespace media
}  // namespace mojo
