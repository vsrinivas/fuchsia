// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "apps/media/services/media_type_converter.fidl.h"
#include "apps/media/src/audio/lpcm_reformatter.h"
#include "apps/media/src/fidl/fidl_packet_consumer.h"
#include "apps/media/src/fidl/fidl_packet_producer.h"
#include "apps/media/src/framework/graph.h"
#include "apps/media/src/media_service/media_service_impl.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace media {

// Fidl agent that decodes a stream.
class LpcmReformatterImpl
    : public MediaServiceImpl::Product<MediaTypeConverter>,
      public MediaTypeConverter {
 public:
  static std::shared_ptr<LpcmReformatterImpl> Create(
      MediaTypePtr input_media_type,
      AudioSampleFormat output_sample_format,
      fidl::InterfaceRequest<MediaTypeConverter> request,
      MediaServiceImpl* owner);

  ~LpcmReformatterImpl() override;

  // MediaTypeConverter implementation.
  void GetOutputType(const GetOutputTypeCallback& callback) override;

  void GetPacketConsumer(
      fidl::InterfaceRequest<MediaPacketConsumer> consumer) override;

  void GetPacketProducer(
      fidl::InterfaceRequest<MediaPacketProducer> producer) override;

 private:
  LpcmReformatterImpl(MediaTypePtr input_media_type,
                      AudioSampleFormat output_sample_format,
                      fidl::InterfaceRequest<MediaTypeConverter> request,
                      MediaServiceImpl* owner);

  Graph graph_;
  std::shared_ptr<FidlPacketConsumer> consumer_;
  std::shared_ptr<LpcmReformatter> reformatter_;
  std::shared_ptr<FidlPacketProducer> producer_;
};

}  // namespace media
