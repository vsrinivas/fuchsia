// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/lpcm_reformatter_impl.h"

#include "apps/media/src/fidl/fidl_type_conversions.h"
#include "lib/ftl/logging.h"

namespace media {

// static
std::shared_ptr<LpcmReformatterImpl> LpcmReformatterImpl::Create(
    MediaTypePtr input_media_type,
    AudioSampleFormat output_sample_format,
    fidl::InterfaceRequest<MediaTypeConverter> request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<LpcmReformatterImpl>(
      new LpcmReformatterImpl(std::move(input_media_type), output_sample_format,
                              std::move(request), owner));
}

LpcmReformatterImpl::LpcmReformatterImpl(
    MediaTypePtr input_media_type,
    AudioSampleFormat output_sample_format,
    fidl::InterfaceRequest<MediaTypeConverter> request,
    MediaServiceImpl* owner)
    : MediaServiceImpl::Product<MediaTypeConverter>(this,
                                                    std::move(request),
                                                    owner),
      consumer_(FidlPacketConsumer::Create()),
      producer_(FidlPacketProducer::Create()) {
  FTL_DCHECK(input_media_type);

  std::unique_ptr<StreamType> input_stream_type =
      input_media_type.To<std::unique_ptr<StreamType>>();
  RCHECK(input_stream_type->medium() == StreamType::Medium::kAudio);
  RCHECK(input_stream_type->audio() != nullptr);

  reformatter_ = LpcmReformatter::Create(*input_stream_type->audio(),
                                         Convert(output_sample_format));
  FTL_DCHECK(reformatter_);

  PartRef consumer_ref = graph_.Add(consumer_);
  PartRef reformatter_ref = graph_.Add(reformatter_);
  PartRef producer_ref = graph_.Add(producer_);

  graph_.ConnectParts(consumer_ref, reformatter_ref);
  graph_.ConnectParts(reformatter_ref, producer_ref);

  consumer_->SetFlushRequestedCallback(
      [this, consumer_ref](const MediaPacketConsumer::FlushCallback& callback) {
        FTL_DCHECK(producer_);
        graph_.FlushOutput(consumer_ref.output());
        producer_->FlushConnection(callback);
      });

  graph_.Prepare();
}

LpcmReformatterImpl::~LpcmReformatterImpl() {}

void LpcmReformatterImpl::GetOutputType(const GetOutputTypeCallback& callback) {
  FTL_DCHECK(reformatter_);
  callback(MediaType::From(reformatter_->output_stream_type()));
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
