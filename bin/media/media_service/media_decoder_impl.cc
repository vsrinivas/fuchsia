// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/media_decoder_impl.h"

#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "lib/fxl/logging.h"

namespace media {

// static
std::shared_ptr<MediaDecoderImpl> MediaDecoderImpl::Create(
    MediaTypePtr input_media_type,
    fidl::InterfaceRequest<MediaTypeConverter> request,
    MediaComponentFactory* owner) {
  return std::shared_ptr<MediaDecoderImpl>(new MediaDecoderImpl(
      std::move(input_media_type), std::move(request), owner));
}

MediaDecoderImpl::MediaDecoderImpl(
    MediaTypePtr input_media_type,
    fidl::InterfaceRequest<MediaTypeConverter> request,
    MediaComponentFactory* owner)
    : MediaComponentFactory::Product<MediaTypeConverter>(this,
                                                         std::move(request),
                                                         owner),
      graph_(owner->task_runner()),
      consumer_(FidlPacketConsumer::Create()),
      producer_(FidlPacketProducer::Create()) {
  FXL_DCHECK(input_media_type);

  std::unique_ptr<StreamType> input_stream_type =
      fxl::To<std::unique_ptr<StreamType>>(input_media_type);

  if (Decoder::Create(*input_stream_type, &decoder_) != Result::kOk) {
    FXL_LOG(WARNING) << "Couldn't find decoder for stream type";
    RCHECK(false);
    return;
  }

  NodeRef consumer_ref = graph_.Add(consumer_);
  NodeRef decoder_ref = graph_.Add(decoder_);
  NodeRef producer_ref = graph_.Add(producer_);

  graph_.ConnectNodes(consumer_ref, decoder_ref);
  graph_.ConnectNodes(decoder_ref, producer_ref);

  consumer_->SetFlushRequestedCallback(
      [this, consumer_ref](bool hold_frame,
                           MediaPacketConsumer::FlushCallback callback) {
        FXL_DCHECK(producer_);
        graph_.FlushOutput(consumer_ref.output(), hold_frame);
        producer_->FlushConnection(hold_frame, callback);
      });

  graph_.Prepare();
}

MediaDecoderImpl::~MediaDecoderImpl() {}

void MediaDecoderImpl::GetOutputType(GetOutputTypeCallback callback) {
  FXL_DCHECK(decoder_);
  callback(std::move(*fxl::To<MediaTypePtr>(decoder_->output_stream_type())));
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
