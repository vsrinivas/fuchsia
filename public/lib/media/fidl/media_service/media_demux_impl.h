// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FACTORY_SERVICE_MEDIA_DEMUX_IMPL_H_
#define SERVICES_MEDIA_FACTORY_SERVICE_MEDIA_DEMUX_IMPL_H_

#include <cstdint>
#include <memory>
#include <vector>

#include "base/single_thread_task_runner.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/services/flog/cpp/flog.h"
#include "mojo/services/media/core/interfaces/media_demux.mojom.h"
#include "mojo/services/media/core/interfaces/seeking_reader.mojom.h"
#include "mojo/services/media/logs/interfaces/media_demux_channel.mojom.h"
#include "services/media/common/mojo_publisher.h"
#include "services/media/factory_service/factory_service.h"
#include "services/media/framework/graph.h"
#include "services/media/framework/parts/demux.h"
#include "services/media/framework_mojo/mojo_packet_producer.h"
#include "services/util/cpp/incident.h"

namespace mojo {
namespace media {

// Mojo agent that decodes a stream.
class MediaDemuxImpl : public MediaFactoryService::Product<MediaDemux>,
                       public MediaDemux {
 public:
  static std::shared_ptr<MediaDemuxImpl> Create(
      InterfaceHandle<SeekingReader> reader,
      InterfaceRequest<MediaDemux> request,
      MediaFactoryService* owner);

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
                 MediaFactoryService* owner);

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

  // Runs the seek callback.
  static void RunSeekCallback(const SeekCallback& callback);

  // Handles the completion of demux initialization.
  void OnDemuxInitialized(Result result);

  // Reports a problem via status.
  void ReportProblem(const std::string& type, const std::string& details);

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
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

#endif  // SERVICES_MEDIA_FACTORY_SERVICE_MEDIA_DEMUX_IMPL_H_
