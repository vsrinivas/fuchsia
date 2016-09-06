// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/services/media_service/media_decoder_impl.h"

#include "apps/media/services/framework_mojo/mojo_type_conversions.h"
#include "lib/ftl/logging.h"

namespace mojo {
namespace media {

// static
std::shared_ptr<MediaDecoderImpl> MediaDecoderImpl::Create(
    MediaTypePtr input_media_type,
    InterfaceRequest<MediaTypeConverter> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<MediaDecoderImpl>(
      new MediaDecoderImpl(input_media_type.Pass(), request.Pass(), owner));
}

MediaDecoderImpl::MediaDecoderImpl(MediaTypePtr input_media_type,
                                   InterfaceRequest<MediaTypeConverter> request,
                                   MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaTypeConverter>(this,
                                                    request.Pass(),
                                                    owner),
      consumer_(MojoPacketConsumer::Create()),
      producer_(MojoPacketProducer::Create()) {
  FTL_DCHECK(input_media_type);

  std::unique_ptr<StreamType> input_stream_type =
      input_media_type.To<std::unique_ptr<StreamType>>();

  if (Decoder::Create(*input_stream_type, &decoder_) != Result::kOk) {
    FTL_LOG(WARNING) << "Couldn't find decoder for stream type";
    UnbindAndReleaseFromOwner();
    return;
  }

  FLOG(log_channel_,
       Config(input_media_type.Pass(),
              MediaType::From(decoder_->output_stream_type()),
              FLOG_ADDRESS(consumer_.get()), FLOG_ADDRESS(producer_.get())));

  PartRef consumer_ref = graph_.Add(consumer_);
  PartRef decoder_ref = graph_.Add(decoder_);
  PartRef producer_ref = graph_.Add(producer_);

  graph_.ConnectParts(consumer_ref, decoder_ref);
  graph_.ConnectParts(decoder_ref, producer_ref);

  consumer_->SetFlushRequestedCallback(
      [this, consumer_ref](const MediaPacketConsumer::FlushCallback& callback) {
        FTL_DCHECK(producer_);
        graph_.FlushOutput(consumer_ref.output());
        producer_->FlushConnection(callback);
      });

  graph_.Prepare();
}

MediaDecoderImpl::~MediaDecoderImpl() {}

void MediaDecoderImpl::GetOutputType(const GetOutputTypeCallback& callback) {
  FTL_DCHECK(decoder_);
  callback.Run(MediaType::From(decoder_->output_stream_type()));
}

void MediaDecoderImpl::GetPacketConsumer(
    mojo::InterfaceRequest<MediaPacketConsumer> consumer) {
  consumer_->Bind(consumer.Pass());
}

void MediaDecoderImpl::GetPacketProducer(
    mojo::InterfaceRequest<MediaPacketProducer> producer) {
  producer_->Bind(producer.Pass());
}

}  // namespace media
}  // namespace mojo
