// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SERVICES_MEDIA_FACTORY_SERVICE_MEDIA_DECODER_IMPL_H_
#define SERVICES_MEDIA_FACTORY_SERVICE_MEDIA_DECODER_IMPL_H_

#include <memory>

#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/services/flog/cpp/flog.h"
#include "mojo/services/media/core/interfaces/media_type_converter.mojom.h"
#include "mojo/services/media/logs/interfaces/media_decoder_channel.mojom.h"
#include "services/media/factory_service/factory_service.h"
#include "services/media/framework/graph.h"
#include "services/media/framework/parts/decoder.h"
#include "services/media/framework_mojo/mojo_packet_consumer.h"
#include "services/media/framework_mojo/mojo_packet_producer.h"

namespace mojo {
namespace media {

// Mojo agent that decodes a stream.
class MediaDecoderImpl
    : public MediaFactoryService::Product<MediaTypeConverter>,
      public MediaTypeConverter {
 public:
  static std::shared_ptr<MediaDecoderImpl> Create(
      MediaTypePtr input_media_type,
      InterfaceRequest<MediaTypeConverter> request,
      MediaFactoryService* owner);

  ~MediaDecoderImpl() override;

  // MediaTypeConverter implementation.
  void GetOutputType(const GetOutputTypeCallback& callback) override;

  void GetPacketConsumer(
      InterfaceRequest<MediaPacketConsumer> consumer) override;

  void GetPacketProducer(
      InterfaceRequest<MediaPacketProducer> producer) override;

 private:
  MediaDecoderImpl(MediaTypePtr input_media_type,
                   InterfaceRequest<MediaTypeConverter> request,
                   MediaFactoryService* owner);

  Graph graph_;
  std::shared_ptr<MojoPacketConsumer> consumer_;
  std::shared_ptr<Decoder> decoder_;
  std::shared_ptr<MojoPacketProducer> producer_;

  FLOG_INSTANCE_CHANNEL(logs::MediaDecoderChannel, log_channel_);
};

}  // namespace media
}  // namespace mojo

#endif  // SERVICES_MEDIA_FACTORY_SERVICE_MEDIA_DECODER_IMPL_H_
