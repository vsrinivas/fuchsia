// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/lpcm_reformatter_impl.h"

#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "lib/fxl/logging.h"

namespace media {

// static
std::shared_ptr<LpcmReformatterImpl> LpcmReformatterImpl::Create(
    MediaTypePtr input_media_type,
    AudioSampleFormat output_sample_format,
    fidl::InterfaceRequest<MediaTypeConverter> request,
    MediaComponentFactory* owner) {
  return std::shared_ptr<LpcmReformatterImpl>(
      new LpcmReformatterImpl(std::move(input_media_type), output_sample_format,
                              std::move(request), owner));
}

LpcmReformatterImpl::LpcmReformatterImpl(
    MediaTypePtr input_media_type,
    AudioSampleFormat output_sample_format,
    fidl::InterfaceRequest<MediaTypeConverter> request,
    MediaComponentFactory* owner)
    : MediaComponentFactory::Product<MediaTypeConverter>(this,
                                                         std::move(request),
                                                         owner),
      graph_(owner->multiproc_task_runner()),
      consumer_(FidlPacketConsumer::Create()),
      producer_(FidlPacketProducer::Create()) {
  FXL_DCHECK(input_media_type);

  std::unique_ptr<StreamType> input_stream_type =
      fxl::To<std::unique_ptr<StreamType>>(input_media_type);
  RCHECK(input_stream_type->medium() == StreamType::Medium::kAudio);
  RCHECK(input_stream_type->audio() != nullptr);

  reformatter_ = LpcmReformatter::Create(
      *input_stream_type->audio(),
      fxl::To<AudioStreamType::SampleFormat>(output_sample_format));
  FXL_DCHECK(reformatter_);

  NodeRef consumer_ref = graph_.Add(consumer_);
  NodeRef reformatter_ref = graph_.Add(reformatter_);
  NodeRef producer_ref = graph_.Add(producer_);

  graph_.ConnectNodes(consumer_ref, reformatter_ref);
  graph_.ConnectNodes(reformatter_ref, producer_ref);

  consumer_->SetFlushRequestedCallback(
      [this, consumer_ref](bool hold_frame,
                           MediaPacketConsumer::FlushCallback callback) {
        FXL_DCHECK(producer_);
        graph_.FlushOutput(consumer_ref.output(), hold_frame);
        producer_->FlushConnection(hold_frame, callback);
      });

  graph_.Prepare();
}

LpcmReformatterImpl::~LpcmReformatterImpl() {}

void LpcmReformatterImpl::GetOutputType(GetOutputTypeCallback callback) {
  FXL_DCHECK(reformatter_);
  callback(fxl::To<MediaType>(reformatter_->output_stream_type()));
}

void LpcmReformatterImpl::GetPacketConsumer(
    fidl::InterfaceRequest<MediaPacketConsumer> request) {
  Retain();
  consumer_->Bind(std::move(request), [this]() { Release(); });
}

void LpcmReformatterImpl::GetPacketProducer(
    fidl::InterfaceRequest<MediaPacketProducer> request) {
  producer_->Bind(std::move(request));
}

}  // namespace media
