// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MEDIA_SERVICES_MEDIA_SERVICE_MEDIA_DECODER_IMPL_H_
#define APPS_MEDIA_SERVICES_MEDIA_SERVICE_MEDIA_DECODER_IMPL_H_

#include <memory>

#include "apps/media/cpp/flog.h"
#include "apps/media/interfaces/logs/media_decoder_channel.mojom.h"
#include "apps/media/interfaces/media_type_converter.mojom.h"
#include "apps/media/services/framework/graph.h"
#include "apps/media/services/framework/parts/decoder.h"
#include "apps/media/services/framework_mojo/mojo_packet_consumer.h"
#include "apps/media/services/framework_mojo/mojo_packet_producer.h"
#include "apps/media/services/media_service/media_service_impl.h"
#include "mojo/public/cpp/bindings/binding.h"

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

#endif  // APPS_MEDIA_SERVICES_MEDIA_SERVICE_MEDIA_DECODER_IMPL_H_
