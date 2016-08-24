// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/media_service/media_demux_impl.h"

#include "apps/media/services/framework/parts/reader_cache.h"
#include "apps/media/services/framework/util/callback_joiner.h"
#include "apps/media/services/framework_mojo/mojo_reader.h"
#include "apps/media/services/framework_mojo/mojo_type_conversions.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mojo {
namespace media {

// static
std::shared_ptr<MediaDemuxImpl> MediaDemuxImpl::Create(
    InterfaceHandle<SeekingReader> reader,
    InterfaceRequest<MediaDemux> request,
    MediaFactoryService* owner) {
  return std::shared_ptr<MediaDemuxImpl>(
      new MediaDemuxImpl(reader.Pass(), request.Pass(), owner));
}

MediaDemuxImpl::MediaDemuxImpl(InterfaceHandle<SeekingReader> reader,
                               InterfaceRequest<MediaDemux> request,
                               MediaFactoryService* owner)
    : MediaFactoryService::Product<MediaDemux>(this, request.Pass(), owner) {
  DCHECK(reader);

  task_runner_ = base::MessageLoop::current()->task_runner();
  DCHECK(task_runner_);

  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        MediaDemuxStatusPtr status = MediaDemuxStatus::New();
        status->metadata = metadata_.Clone();
        status->problem = problem_.Clone();
        callback.Run(version, status.Pass());
      });

  std::shared_ptr<Reader> reader_ptr = MojoReader::Create(reader.Pass());
  if (!reader_ptr) {
    ReportProblem(Problem::kProblemInternal, "couldn't create reader");
    return;
  }

  std::shared_ptr<ReaderCache> reader_cache_ptr =
      ReaderCache::Create(reader_ptr);
  if (!reader_cache_ptr) {
    ReportProblem(Problem::kProblemInternal, "couldn't create reader cache");
    return;
  }

  demux_ = Demux::Create(std::shared_ptr<Reader>(reader_cache_ptr));
  if (!demux_) {
    ReportProblem(Problem::kProblemInternal, "couldn't create demux");
    return;
  }

  demux_->SetStatusCallback([this](const std::unique_ptr<Metadata>& metadata,
                                   const std::string& problem_type,
                                   const std::string& problem_details) {
    metadata_ = MediaMetadata::From(metadata);
    if (problem_type.empty()) {
      problem_.reset();
      status_publisher_.SendUpdates();
    } else {
      ReportProblem(problem_type, problem_details);
      // ReportProblem calls status_publisher_.SendUpdates();
    }
  });

  demux_->WhenInitialized([this](Result result) {
    task_runner_->PostTask(FROM_HERE,
                           base::Bind(&MediaDemuxImpl::OnDemuxInitialized,
                                      base::Unretained(this), result));
  });
}

MediaDemuxImpl::~MediaDemuxImpl() {}

void MediaDemuxImpl::OnDemuxInitialized(Result result) {
  demux_part_ = graph_.Add(demux_);

  const std::vector<Demux::DemuxStream*>& demux_streams = demux_->streams();
  for (Demux::DemuxStream* demux_stream : demux_streams) {
    streams_.push_back(std::unique_ptr<Stream>(
        new Stream(demux_part_.output(demux_stream->index()),
                   demux_stream->stream_type(), &graph_)));
    FLOG(log_channel_,
         NewStream(streams_.size() - 1, streams_.back()->media_type(),
                   FLOG_ADDRESS(streams_.back()->producer().get())));
  }

  graph_.Prepare();

  status_publisher_.SendUpdates();

  init_complete_.Occur();
}

void MediaDemuxImpl::ReportProblem(const std::string& type,
                                   const std::string& details) {
  problem_ = Problem::New();
  problem_->type = type;
  problem_->details = details;
  status_publisher_.SendUpdates();
}

void MediaDemuxImpl::Describe(const DescribeCallback& callback) {
  init_complete_.When([this, callback]() {
    Array<MediaTypePtr> result = Array<MediaTypePtr>::New(streams_.size());
    for (size_t i = 0; i < streams_.size(); i++) {
      MediaSourceStreamDescriptorPtr descriptor =
          MediaSourceStreamDescriptor::New();
      result[i] = streams_[i]->media_type();
    }

    callback.Run(result.Pass());
  });
}

void MediaDemuxImpl::GetPacketProducer(
    uint32_t stream_index,
    InterfaceRequest<MediaPacketProducer> producer) {
  RCHECK(init_complete_.occurred());

  if (stream_index >= streams_.size()) {
    return;
  }

  streams_[stream_index]->BindPacketProducer(producer.Pass());
}

void MediaDemuxImpl::GetStatus(uint64_t version_last_seen,
                               const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void MediaDemuxImpl::Flush(const FlushCallback& callback) {
  RCHECK(init_complete_.occurred());

  graph_.FlushAllOutputs(demux_part_);

  std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

  for (std::unique_ptr<Stream>& stream : streams_) {
    stream->FlushConnection(callback_joiner->NewCallback());
  }

  callback_joiner->WhenJoined(callback);
}

void MediaDemuxImpl::Seek(int64_t position, const SeekCallback& callback) {
  RCHECK(init_complete_.occurred());

  demux_->Seek(position, [this, callback]() {
    task_runner_->PostTask(FROM_HERE, base::Bind(&RunSeekCallback, callback));
  });
}

// static
void MediaDemuxImpl::RunSeekCallback(const SeekCallback& callback) {
  callback.Run();
}

MediaDemuxImpl::Stream::Stream(OutputRef output,
                               std::unique_ptr<StreamType> stream_type,
                               Graph* graph)
    : stream_type_(std::move(stream_type)), graph_(graph), output_(output) {
  DCHECK(stream_type_);
  DCHECK(graph);

  producer_ = MojoPacketProducer::Create();
  graph_->ConnectOutputToPart(output_, graph_->Add(producer_));
}

MediaDemuxImpl::Stream::~Stream() {}

MediaTypePtr MediaDemuxImpl::Stream::media_type() const {
  return MediaType::From(stream_type_);
}

void MediaDemuxImpl::Stream::BindPacketProducer(
    InterfaceRequest<MediaPacketProducer> producer) {
  DCHECK(producer_);
  producer_->Bind(producer.Pass());
}

void MediaDemuxImpl::Stream::FlushConnection(
    const MojoPacketProducer::FlushConnectionCallback callback) {
  DCHECK(producer_);
  producer_->FlushConnection(callback);
}

}  // namespace media
}  // namespace mojo
