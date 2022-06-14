// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_BUILDER_CONNECTOR_H_
#define SRC_MEDIA_VNEXT_LIB_BUILDER_CONNECTOR_H_

#include <fuchsia/audio/cpp/fidl.h>
#include <fuchsia/audiovideo/cpp/fidl.h>
#include <fuchsia/media2/cpp/fidl.h>
#include <fuchsia/mediastreams/cpp/fidl.h>
#include <fuchsia/video/cpp/fidl.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/eventpair.h>

#include <utility>

#include "src/media/vnext/lib/builder/audio_conversion_pipeline.h"
#include "src/media/vnext/lib/builder/create_buffer_collection.h"
#include "src/media/vnext/lib/builder/video_conversion_pipeline.h"
#include "src/media/vnext/lib/stream_io/input.h"
#include "src/media/vnext/lib/stream_io/output.h"
#include "src/media/vnext/lib/stream_io/packet.h"
#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {

// Objects that produce or consume streams have 'connect' methods that allow output connectors to
// be connected to input connectors. Those methods have varying signatures based on what parameters
// the caller can control for a given connection. For example, the method for connecting to the
// output of a demux stream has no 'format' parameter, because the format is determined by the
// contents of the file read by the demux.
//
// This file establishes standard signatures for output and input connection (see
// |OutputConnector::ConnectOutputStream| and |InputConnector::ConnectInputStream|) using templates.
// Specializations of those templates adapt the standard signatures to the specific signatures of
// the various connectors.
//
// While the |OutputConnector| and |InputConnector| templates are not particularly useful by
// themselves, they allow us to write the |Connect| method at the bottom of this file, which will
// connect any compatible output and input connectors for which |OutputConnector| and
// |InputConnector| specializations exist. This is, of course, valuable in template code in which
// the types of the objects being connected are not known. It's also helpful in other cases, because
// it allows a connection to be established by the invocation of a single async method (|Connect|
// below) rather than by the invocation of two async methods whose results must be joined.

// |OutputConnector| provides a generic way to connect to the output of a media service or other
// object. Specialiations of this template convert from a generic |ConnectOutputStream| call to the
// |Producer|-specific code required to make the connection.
//
// |Producer| is typically an interface pointer for a FIDL media service that has a single output,
// but it be any type for which connecting in this way makes sense.
template <typename Producer, typename Format>
struct OutputConnector {
  // Begins connecting the output of |Producer| and completes |completer| when the connection is
  // complete or an error occurs. |format|, |compression| and |timestamp_units| are passed as const
  // references, because these parameters are often not used, and we want to avoid e.g. cloning
  // values if they may not be used.
  static fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectOutputStream(
      Producer& producer, zx::eventpair buffer_collection_token, const Format& format,
      const fuchsia::mediastreams::CompressionPtr& compression,
      const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
      fuchsia::media2::StreamSinkHandle handle);
};

// |InputConnector| provides a generic way to connect to the input of a media service or other
// object. Specialiations of this template convert from a generic |ConnectInputStream| call to the
// |Consumer|-specific code required to make the connection.
//
// |Consumer| is typically an interface pointer for a FIDL media service that has a single input,
// but it be any type for which connecting in this way makes sense.
template <typename Consumer, typename Format>
struct InputConnector {
  // Begins connecting the input of |Consumer| and completes |completer| when the connection is
  // complete or an error occurs. |format|, |compression| and |timestamp_units| are passed as const
  // references, because these parameters are often not used, and we want to avoid e.g. cloning
  // values if they may not be used.
  static fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectInputStream(
      Consumer& consumer, zx::eventpair buffer_collection_token, const Format& format,
      const fuchsia::mediastreams::CompressionPtr& compression,
      const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
      fidl::InterfaceRequest<fuchsia::media2::StreamSink> request);
};

// |InputConnector| specialization for |fuchsia::audio::ConsumerPtr|.
template <>
struct InputConnector<fuchsia::audio::ConsumerPtr, fuchsia::mediastreams::AudioFormat> {
  static fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectInputStream(
      fuchsia::audio::ConsumerPtr& consumer, zx::eventpair buffer_collection_token,
      const fuchsia::mediastreams::AudioFormat& format,
      const fuchsia::mediastreams::CompressionPtr& compression,
      const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
      fidl::InterfaceRequest<fuchsia::media2::StreamSink> request) {
    fpromise::bridge<void, fuchsia::media2::ConnectionError> bridge;
    consumer->ConnectInputStream(
        std::move(buffer_collection_token), fidl::Clone(format), fidl::Clone(compression),
        fidl::Clone(timestamp_units), std::move(request),
        [completer = std::move(bridge.completer)](
            const fuchsia::audio::Consumer_ConnectInputStream_Result& result) mutable {
          if (result.is_response()) {
            completer.complete_ok();
          } else {
            completer.complete_error(result.err());
          }
        });

    return bridge.consumer.promise();
  }
};

// |InputConnector| specialization for |fuchsia::video::ConsumerPtr|.
template <>
struct InputConnector<fuchsia::video::ConsumerPtr, fuchsia::mediastreams::VideoFormat> {
  static fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectInputStream(
      fuchsia::video::ConsumerPtr& consumer, zx::eventpair buffer_collection_token,
      const fuchsia::mediastreams::VideoFormat& format,
      const fuchsia::mediastreams::CompressionPtr& compression,
      const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
      fidl::InterfaceRequest<fuchsia::media2::StreamSink> request) {
    fpromise::bridge<void, fuchsia::media2::ConnectionError> bridge;
    consumer->ConnectInputStream(
        std::move(buffer_collection_token), fidl::Clone(format), fidl::Clone(compression),
        fidl::Clone(timestamp_units), std::move(request),
        [completer = std::move(bridge.completer)](
            const fuchsia::video::Consumer_ConnectInputStream_Result& result) mutable {
          if (result.is_response()) {
            completer.complete_ok();
          } else {
            completer.complete_error(result.err());
          }
        });

    return bridge.consumer.promise();
  }
};

// |InputConnector| specialization for |AudioConversionPipeline|.
template <>
struct InputConnector<AudioConversionPipeline, fuchsia::mediastreams::AudioFormat> {
  static fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectInputStream(
      AudioConversionPipeline& consumer, zx::eventpair buffer_collection_token,
      const fuchsia::mediastreams::AudioFormat& format,
      const fuchsia::mediastreams::CompressionPtr& compression,
      const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
      fidl::InterfaceRequest<fuchsia::media2::StreamSink> request) {
    return consumer.ConnectInputStream(std::move(buffer_collection_token),
                                       fidl::Clone(timestamp_units), std::move(request));
  }
};

// |OutputConnector| specialization for |AudioConversionPipeline|.
template <>
struct OutputConnector<AudioConversionPipeline, fuchsia::mediastreams::AudioFormat> {
  static fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectOutputStream(
      AudioConversionPipeline& producer, zx::eventpair buffer_collection_token,
      const fuchsia::mediastreams::AudioFormat& format,
      const fuchsia::mediastreams::CompressionPtr& compression,
      const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
      fuchsia::media2::StreamSinkHandle handle) {
    return producer.ConnectOutputStream(std::move(buffer_collection_token), std::move(handle));
  }
};

// |InputConnector| specialization for |VideoConversionPipeline|.
template <>
struct InputConnector<VideoConversionPipeline, fuchsia::mediastreams::VideoFormat> {
  static fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectInputStream(
      VideoConversionPipeline& consumer, zx::eventpair buffer_collection_token,
      const fuchsia::mediastreams::VideoFormat& format,
      const fuchsia::mediastreams::CompressionPtr& compression,
      const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
      fidl::InterfaceRequest<fuchsia::media2::StreamSink> request) {
    return consumer.ConnectInputStream(std::move(buffer_collection_token),
                                       fidl::Clone(timestamp_units), std::move(request));
  }
};

// |OutputConnector| specialization for |VideoConversionPipeline|.
template <>
struct OutputConnector<VideoConversionPipeline, fuchsia::mediastreams::VideoFormat> {
  static fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectOutputStream(
      VideoConversionPipeline& producer, zx::eventpair buffer_collection_token,
      const fuchsia::mediastreams::VideoFormat& format,
      const fuchsia::mediastreams::CompressionPtr& compression,
      const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
      fuchsia::media2::StreamSinkHandle handle) {
    return producer.ConnectOutputStream(std::move(buffer_collection_token), std::move(handle));
  }
};

// |OutputConnector| specialization for |fuchsia::audiovideo::ProducerStreamPtr|. |Format| remains
// a type parameter, because producer streams can be of any supported medium (audio, video, etc).
template <typename Format>
struct OutputConnector<fuchsia::audiovideo::ProducerStreamPtr, Format> {
  static fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectOutputStream(
      fuchsia::audiovideo::ProducerStreamPtr& producer, zx::eventpair buffer_collection_token,
      const Format& format, const fuchsia::mediastreams::CompressionPtr& compression,
      const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
      fuchsia::media2::StreamSinkHandle handle) {
    fpromise::bridge<void, fuchsia::media2::ConnectionError> bridge;
    producer->Connect(
        std::move(buffer_collection_token), std::move(handle),
        [completer = std::move(bridge.completer)](
            const fuchsia::audiovideo::ProducerStream_Connect_Result& result) mutable {
          if (result.is_response()) {
            completer.complete_ok();
          } else {
            completer.complete_error(result.err());
          }
        });

    return bridge.consumer.promise();
  }
};

// |OutputConnector| specialization for |fuchsia::audio::ProducerPtr|.
template <>
struct OutputConnector<fuchsia::audio::ProducerPtr, fuchsia::mediastreams::AudioFormat> {
  static fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectOutputStream(
      fuchsia::audio::ProducerPtr& producer, zx::eventpair buffer_collection_token,
      const fuchsia::mediastreams::AudioFormat& format,
      const fuchsia::mediastreams::CompressionPtr& compression,
      const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
      fuchsia::media2::StreamSinkHandle handle) {
    FX_CHECK(!compression);
    FX_CHECK(timestamp_units);
    fpromise::bridge<void, fuchsia::media2::ConnectionError> bridge;
    producer->ConnectOutputStream(
        std::move(buffer_collection_token), fidl::Clone(format), fidl::Clone(*timestamp_units),
        std::move(handle),
        [completer = std::move(bridge.completer)](
            const fuchsia::audio::Producer_ConnectOutputStream_Result& result) mutable {
          if (result.is_response()) {
            completer.complete_ok();
          } else {
            completer.complete_error(result.err());
          }
        });

    return bridge.consumer.promise();
  }
};

// Connects the output of |producer| to the input of |consumer|. |producer| can be any type for
// which there is an |OutputConnector| specialization. |consumer| can be any type for which there
// is an |InputConnector| specialization. Typically, both producer and consumer are FIDL interface
// pointers to media service objects.
template <typename Producer, typename Consumer, typename Format>
[[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError> Connect(
    Producer& producer, Consumer& consumer, const Format& format,
    const fuchsia::mediastreams::CompressionPtr& compression,
    const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
    fuchsia::media2::BufferProvider& buffer_provider) {
  auto [consumer_token, producer_token] = CreateBufferCollection(buffer_provider);

  fuchsia::media2::StreamSinkHandle handle;
  auto request = handle.NewRequest();

  return fpromise::join_promises(InputConnector<Consumer, Format>::ConnectInputStream(
                                     consumer, std::move(consumer_token), format, compression,
                                     timestamp_units, std::move(request)),
                                 OutputConnector<Producer, Format>::ConnectOutputStream(
                                     producer, std::move(producer_token), format, compression,
                                     timestamp_units, std::move(handle)))
      .then(
          [](fpromise::result<std::tuple<fpromise::result<void, fuchsia::media2::ConnectionError>,
                                         fpromise::result<void, fuchsia::media2::ConnectionError>>>&
                 result) -> fpromise::result<void, fuchsia::media2::ConnectionError> {
            FX_CHECK(result.is_ok());
            auto& results = result.value();

            if (std::get<0>(results).is_error()) {
              return fpromise::error(std::get<0>(results).error());
            }

            if (std::get<1>(results).is_error()) {
              return fpromise::error(std::get<1>(results).error());
            }

            return fpromise::ok();
          });
}

// Connects the output of |producer| to an in-process |Input|. |producer| can be any type for which
// there is an |OutputConnector| specialization.
template <typename Producer, typename Packet, typename Format>
[[nodiscard]] fpromise::promise<std::unique_ptr<typename Input<Packet>::Connection>,
                                fuchsia::media2::ConnectionError>
Connect(Producer& producer, Input<Packet>& input, Thread fidl_thread,
        fuchsia::media2::BufferConstraints constraints, const Format& format,
        const fuchsia::mediastreams::CompressionPtr& compression,
        const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
        fuchsia::media2::BufferProvider& buffer_provider) {
  auto [consumer_token, producer_token] = CreateBufferCollection(buffer_provider);

  fuchsia::media2::StreamSinkHandle handle;
  auto request = handle.NewRequest();

  return fpromise::join_promises(input.Connect(fidl_thread, std::move(request), buffer_provider,
                                               std::move(consumer_token), std::move(constraints)),
                                 OutputConnector<Producer, Format>::ConnectOutputStream(
                                     producer, std::move(producer_token), format, compression,
                                     timestamp_units, std::move(handle)))
      .then([](fpromise::result<
                std::tuple<fpromise::result<std::unique_ptr<typename Input<Packet>::Connection>,
                                            fuchsia::media2::ConnectionError>,
                           fpromise::result<void, fuchsia::media2::ConnectionError>>>& result)
                -> fpromise::result<std::unique_ptr<typename Input<Packet>::Connection>,
                                    fuchsia::media2::ConnectionError> {
        FX_CHECK(result.is_ok());
        auto& results = result.value();

        if (std::get<0>(results).is_error()) {
          return fpromise::error(std::get<0>(results).error());
        }

        if (std::get<1>(results).is_error()) {
          return fpromise::error(std::get<1>(results).error());
        }

        return fpromise::ok(std::get<0>(results).take_value());
      });
}

// Connects the output of an in-process |Output| to the input of |consumer|. |consumer| can be any
// type for which there is an |InputConnector| specialization.
template <typename Consumer, typename Packet, typename Format>
[[nodiscard]] fpromise::promise<std::unique_ptr<typename Output<Packet>::Connection>,
                                fuchsia::media2::ConnectionError>
Connect(Output<Packet>& output, Thread fidl_thread, fuchsia::media2::BufferConstraints constraints,
        Consumer& consumer, const Format& format,
        const fuchsia::mediastreams::CompressionPtr& compression,
        const fuchsia::media2::PacketTimestampUnitsPtr& timestamp_units,
        fuchsia::media2::BufferProvider& buffer_provider) {
  auto [consumer_token, producer_token] = CreateBufferCollection(buffer_provider);

  fuchsia::media2::StreamSinkHandle handle;
  auto request = handle.NewRequest();

  return fpromise::join_promises(InputConnector<Consumer, Format>::ConnectInputStream(
                                     consumer, std::move(consumer_token), format, compression,
                                     timestamp_units, std::move(request)),
                                 output.Connect(fidl_thread, std::move(handle), buffer_provider,
                                                std::move(producer_token), std::move(constraints)))
      .then([](fpromise::result<
                std::tuple<fpromise::result<void, fuchsia::media2::ConnectionError>,
                           fpromise::result<std::unique_ptr<typename Output<Packet>::Connection>,
                                            fuchsia::media2::ConnectionError>>>& result)
                -> fpromise::result<std::unique_ptr<typename Output<Packet>::Connection>,
                                    fuchsia::media2::ConnectionError> {
        FX_CHECK(result.is_ok());
        auto& results = result.value();

        if (std::get<0>(results).is_error()) {
          return fpromise::error(std::get<0>(results).error());
        }

        if (std::get<1>(results).is_error()) {
          return fpromise::error(std::get<1>(results).error());
        }

        return fpromise::ok(std::get<1>(results).take_value());
      });
}

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_BUILDER_CONNECTOR_H_
