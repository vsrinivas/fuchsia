// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/media_decoder_impl.h"

#include "apps/media/src/fidl/fidl_type_conversions.h"
#include "lib/ftl/logging.h"

namespace media {

// static
std::shared_ptr<MediaDecoderImpl> MediaDecoderImpl::Create(
    MediaTypePtr input_media_type,
    fidl::InterfaceRequest<MediaTypeConverter> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<MediaDecoderImpl>(new MediaDecoderImpl(
      std::move(input_media_type), std::move(request), owner));
}

MediaDecoderImpl::MediaDecoderImpl(
    MediaTypePtr input_media_type,
    fidl::InterfaceRequest<MediaTypeConverter> request,
    MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaTypeConverter>(this,
                                                    std::move(request),
                                                    owner),
      consumer_(FidlPacketConsumer::Create()),
      producer_(FidlPacketProducer::Create()) {
  FTL_DCHECK(input_media_type);

  FLOG(log_channel_, BoundAs(FLOG_BINDING_KOID(binding()), "decoder"));

  std::unique_ptr<StreamType> input_stream_type =
      input_media_type.To<std::unique_ptr<StreamType>>();

  if (Decoder::Create(*input_stream_type, &decoder_) != Result::kOk) {
    FTL_LOG(WARNING) << "Couldn't find decoder for stream type";
    RCHECK(false);
    return;
  }

  FLOG(log_channel_,
       Config(std::move(input_media_type),
              MediaType::From(decoder_->output_stream_type()),
              FLOG_ADDRESS(consumer_.get()), FLOG_ADDRESS(producer_.get())));

  NodeRef consumer_ref = graph_.Add(consumer_);
  NodeRef decoder_ref = graph_.Add(decoder_);
  NodeRef producer_ref = graph_.Add(producer_);

  graph_.ConnectNodes(consumer_ref, decoder_ref);
  graph_.ConnectNodes(decoder_ref, producer_ref);

  consumer_->SetFlushRequestedCallback(
      [this, consumer_ref](bool hold_frame,
                           const MediaPacketConsumer::FlushCallback& callback) {
        FTL_DCHECK(producer_);
        graph_.FlushOutput(consumer_ref.output(), hold_frame);
        producer_->FlushConnection(hold_frame, callback);
      });

  graph_.Prepare();
}

MediaDecoderImpl::~MediaDecoderImpl() {}

void MediaDecoderImpl::GetOutputType(const GetOutputTypeCallback& callback) {
  FTL_DCHECK(decoder_);
  callback(MediaType::From(decoder_->output_stream_type()));
}

void MediaDecoderImpl::GetPacketConsumer(
    fidl::InterfaceRequest<MediaPacketConsumer> request) {
  Retain();
  consumer_->Bind(std::move(request), [this]() { Release(); });
}

void MediaDecoderImpl::GetPacketProducer(
    fidl::InterfaceRequest<MediaPacketProducer> request) {
  producer_->Bind(std::move(request));
}

}  // namespace media
