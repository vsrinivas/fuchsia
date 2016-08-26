// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/media_service/media_source_impl.h"

#include "apps/media/services/framework/util/callback_joiner.h"
#include "apps/media/services/framework/util/conversion_pipeline_builder.h"
#include "apps/media/services/framework/util/formatting.h"
#include "apps/media/services/framework_mojo/mojo_reader.h"
#include "apps/media/services/framework_mojo/mojo_type_conversions.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mojo {
namespace media {

// static
std::shared_ptr<MediaSourceImpl> MediaSourceImpl::Create(
    InterfaceHandle<SeekingReader> reader,
    const Array<MediaTypeSetPtr>& allowed_media_types,
    InterfaceRequest<MediaSource> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<MediaSourceImpl>(new MediaSourceImpl(
      reader.Pass(), allowed_media_types, request.Pass(), owner));
}

MediaSourceImpl::MediaSourceImpl(
    InterfaceHandle<SeekingReader> reader,
    const Array<MediaTypeSetPtr>& allowed_media_types,
    InterfaceRequest<MediaSource> request,
    MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaSource>(this, request.Pass(), owner),
      allowed_media_types_(allowed_media_types.Clone()) {
  DCHECK(reader);

  task_runner_ = base::MessageLoop::current()->task_runner();
  DCHECK(task_runner_);

  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        MediaSourceStatusPtr status = MediaSourceStatus::New();
        status->metadata = metadata_.Clone();
        status->problem = problem_.Clone();
        callback.Run(version, status.Pass());
      });

  std::shared_ptr<Reader> reader_ptr = MojoReader::Create(reader.Pass());
  if (!reader_ptr) {
    LOG(ERROR) << "couldn't create reader";
    // TODO(dalesat): Add problem reporting.
    return;
  }

  demux_ = Demux::Create(reader_ptr);
  if (!demux_) {
    LOG(ERROR) << "couldn't create demux";
    // TODO(dalesat): Add problem reporting.
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
                           base::Bind(&MediaSourceImpl::OnDemuxInitialized,
                                      base::Unretained(this), result));
  });
}

MediaSourceImpl::~MediaSourceImpl() {}

void MediaSourceImpl::OnDemuxInitialized(Result result) {
  demux_part_ = graph_.Add(demux_);

  const std::vector<Demux::DemuxStream*>& demux_streams = demux_->streams();
  for (Demux::DemuxStream* demux_stream : demux_streams) {
    streams_.push_back(std::unique_ptr<Stream>(new Stream(
        demux_part_.output(demux_stream->index()), demux_stream->stream_type(),
        allowed_media_types_.To<std::unique_ptr<
            std::vector<std::unique_ptr<media::StreamTypeSet>>>>(),
        &graph_)));
  }

  allowed_media_types_.reset();

  init_complete_.Occur();
}

void MediaSourceImpl::ReportProblem(const std::string& type,
                                    const std::string& details) {
  problem_ = Problem::New();
  problem_->type = type;
  problem_->details = details;
  status_publisher_.SendUpdates();
}

void MediaSourceImpl::GetStreams(const GetStreamsCallback& callback) {
  init_complete_.When([this, callback]() {
    Array<MediaSourceStreamDescriptorPtr> result =
        Array<MediaSourceStreamDescriptorPtr>::New(streams_.size());
    for (size_t i = 0; i < streams_.size(); i++) {
      MediaSourceStreamDescriptorPtr descriptor =
          MediaSourceStreamDescriptor::New();
      descriptor->index = i;
      descriptor->media_type = streams_[i]->media_type();
      descriptor->original_media_type = streams_[i]->original_media_type();
      result[i] = descriptor.Pass();
    }
    callback.Run(result.Pass());
  });
}

void MediaSourceImpl::GetPacketProducer(
    uint32_t stream_index,
    InterfaceRequest<MediaPacketProducer> producer) {
  RCHECK(init_complete_.occurred());

  if (stream_index >= streams_.size()) {
    return;
  }

  streams_[stream_index]->GetPacketProducer(producer.Pass());
}

void MediaSourceImpl::GetStatus(uint64_t version_last_seen,
                                const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void MediaSourceImpl::Prepare(const PrepareCallback& callback) {
  RCHECK(init_complete_.occurred());

  for (std::unique_ptr<Stream>& stream : streams_) {
    stream->EnsureSink();
  }
  graph_.Prepare();
  callback.Run();
  status_publisher_.SendUpdates();
}

void MediaSourceImpl::Flush(const FlushCallback& callback) {
  RCHECK(init_complete_.occurred());

  graph_.FlushAllOutputs(demux_part_);

  std::shared_ptr<CallbackJoiner> callback_joiner = CallbackJoiner::Create();

  for (std::unique_ptr<Stream>& stream : streams_) {
    stream->FlushConnection(callback_joiner->NewCallback());
  }

  callback_joiner->WhenJoined(callback);
}

void MediaSourceImpl::Seek(int64_t position, const SeekCallback& callback) {
  RCHECK(init_complete_.occurred());

  demux_->Seek(position, [this, callback]() {
    task_runner_->PostTask(FROM_HERE, base::Bind(&RunSeekCallback, callback));
  });
}

// static
void MediaSourceImpl::RunSeekCallback(const SeekCallback& callback) {
  callback.Run();
}

MediaSourceImpl::Stream::Stream(
    OutputRef output,
    std::unique_ptr<StreamType> stream_type,
    const std::unique_ptr<std::vector<std::unique_ptr<StreamTypeSet>>>&
        allowed_stream_types,
    Graph* graph)
    : original_stream_type_(std::move(stream_type)), graph_(graph) {
  DCHECK(original_stream_type_);
  DCHECK(graph);

  output_ = output;

  if (allowed_stream_types == nullptr) {
    // No conversion requested.
    stream_type_ = SafeClone(original_stream_type_);
  } else if (!BuildConversionPipeline(*original_stream_type_,
                                      *allowed_stream_types, graph, &output_,
                                      &stream_type_)) {
    // Can't convert to any allowed type.
    // TODO(dalesat): Indicate this in some way other than blowing up.
    LOG(ERROR) << "can't convert to any allowed type";
    abort();
  }
}

MediaSourceImpl::Stream::~Stream() {}

MediaTypePtr MediaSourceImpl::Stream::media_type() const {
  return MediaType::From(stream_type_);
}

MediaTypePtr MediaSourceImpl::Stream::original_media_type() const {
  return MediaType::From(original_stream_type_);
}

void MediaSourceImpl::Stream::GetPacketProducer(
    InterfaceRequest<MediaPacketProducer> producer) {
  if (!producer_) {
    producer_ = MojoPacketProducer::Create();
    graph_->ConnectOutputToPart(output_, graph_->Add(producer_));
  }

  producer_->Bind(producer.Pass());
}

void MediaSourceImpl::Stream::EnsureSink() {
  if (producer_ == nullptr) {
    null_sink_ = NullSink::Create();
    graph_->ConnectOutputToPart(output_, graph_->Add(null_sink_));
  }
}

void MediaSourceImpl::Stream::FlushConnection(
    const MojoPacketProducer::FlushConnectionCallback callback) {
  if (producer_ != nullptr) {
    producer_->FlushConnection(callback);
  } else {
    callback.Run();
  }
}

}  // namespace media
}  // namespace mojo
