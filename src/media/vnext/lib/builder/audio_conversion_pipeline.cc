// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/vnext/lib/builder/audio_conversion_pipeline.h"

#include <lib/fpromise/bridge.h>

#include "src/lib/fostr/fidl/fuchsia/mediastreams/formatting.h"
#include "src/lib/fostr/zx_types.h"
#include "src/media/vnext/lib/builder/create_buffer_collection.h"

namespace fmlib {

// static
std::unique_ptr<AudioConversionPipeline> AudioConversionPipeline::Create(
    const fuchsia::mediastreams::AudioFormat& format,
    const fuchsia::mediastreams::CompressionPtr& input_compression,
    const std::unique_ptr<std::string>& output_compression_type,
    ServiceProvider& service_provider) {
  std::vector<std::string> output_supported_compression_types;
  if (output_compression_type) {
    output_supported_compression_types.push_back(*output_compression_type);
  }

  return Create(format, input_compression, output_supported_compression_types,
                !output_compression_type, service_provider);
}

//  static
std::unique_ptr<AudioConversionPipeline> AudioConversionPipeline::Create(
    const fuchsia::mediastreams::AudioFormat& format,
    const fuchsia::mediastreams::CompressionPtr& input_compression,
    const std::vector<std::string>& output_supported_compression_types,
    bool output_supports_uncompressed, ServiceProvider& service_provider) {
  if (!input_compression) {
    // Uncompressed in.
    if (output_supports_uncompressed) {
      // Uncompressed -> uncompressed.
      return nullptr;
    } else {
      // Encode uncompressed.
      return std::make_unique<AudioConversionPipeline>(
          fidl::Clone(format), nullptr, fidl::Clone(output_supported_compression_types), false,
          service_provider);
    }
  } else {
    // Compressed in.
    if (output_supports_uncompressed) {
      // Decode compressed.
      return std::make_unique<AudioConversionPipeline>(
          fidl::Clone(format), fidl::Clone(input_compression), std::vector<std::string>(), true,
          service_provider);
    } else {
      // Compressed -> compressed.
      if (std::find(output_supported_compression_types.begin(),
                    output_supported_compression_types.end(),
                    input_compression->type) != output_supported_compression_types.end()) {
        // Input compression type is supported.
        return nullptr;
      }

      // Transcode.
      return std::make_unique<AudioConversionPipeline>(
          fidl::Clone(format), fidl::Clone(input_compression),
          fidl::Clone(output_supported_compression_types), false, service_provider);
    }
  }
}

[[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError>
AudioConversionPipeline::ConnectInputStream(
    zx::eventpair buffer_collection_token, fuchsia::media2::PacketTimestampUnitsPtr timestamp_units,
    fidl::InterfaceRequest<fuchsia::media2::StreamSink> request) {
  timestamp_units_ = std::move(timestamp_units);

  if (!compression_) {
    FX_CHECK(!output_supports_uncompressed_);
    return BuildForEncode(std::move(buffer_collection_token), std::move(request));
  } else if (output_supports_uncompressed_) {
    return BuildForDecode(std::move(buffer_collection_token), std::move(request));
  } else {
    return BuildForTranscode(std::move(buffer_collection_token), std::move(request));
  }
}

[[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError>
AudioConversionPipeline::ConnectOutputStream(zx::eventpair buffer_collection_token,
                                             fuchsia::media2::StreamSinkHandle handle) {
  FX_CHECK(decoder_);
  fpromise::bridge<void, fuchsia::media2::ConnectionError> bridge;
  decoder_->ConnectOutputStream(
      std::move(buffer_collection_token), std::move(handle),
      [completer = std::move(bridge.completer)](
          const fuchsia::audio::Decoder_ConnectOutputStream_Result& result) mutable {
        if (result.is_err()) {
          completer.complete_error(result.err());
          return;
        }

        completer.complete_ok();
      });

  return bridge.consumer.promise();
}

[[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError>
AudioConversionPipeline::BuildForDecode(
    zx::eventpair buffer_collection_token,
    fidl::InterfaceRequest<fuchsia::media2::StreamSink> request) {
  auto decoder_creator = service_provider_.ConnectToService<fuchsia::audio::DecoderCreator>();

  FX_CHECK(compression_);
  decoder_creator->Create(fidl::Clone(format_), std::move(*compression_), decoder_.NewRequest());
  compression_ = nullptr;

  decoder_.set_error_handler([](zx_status_t status) {
    // TODO(dalesat): Take action.
    FX_PLOGS(ERROR, status) << "Decoder channel closed";
  });

  decoder_.events().OnInputStreamDisconnected = []() {
    // TODO(dalesat): Take action.
    FX_LOGS(INFO) << "Decoder event OnInputStreamDisconnected";
  };

  decoder_.events().OnOutputStreamDisconnected = [](zx_status_t status) {
    // TODO(dalesat): Take action.
    FX_PLOGS(INFO, status) << "Decoder event OnOutputStreamDisconnected";
  };

  fpromise::bridge<void, fuchsia::media2::ConnectionError> bridge;
  decoder_->ConnectInputStream(
      std::move(buffer_collection_token), fidl::Clone(timestamp_units_), std::move(request),
      [this, completer = std::move(bridge.completer)](
          const fuchsia::audio::Decoder_ConnectInputStream_Result& result) mutable {
        if (result.is_err()) {
          completer.complete_error(result.err());
          return;
        }

        decoder_.events().OnNewOutputStreamAvailable =
            [this, completer = std::move(completer)](
                fuchsia::mediastreams::AudioFormat format,
                fuchsia::media2::PacketTimestampUnitsPtr timestamp_units) mutable {
              format_ = std::move(format);
              output_stream_available_ = true;
              completer.complete_ok();
            };
      });

  return bridge.consumer.promise();
}

[[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError>
AudioConversionPipeline::BuildForEncode(
    zx::eventpair buffer_collection_token,
    fidl::InterfaceRequest<fuchsia::media2::StreamSink> request) {
  // TODO(dalesat): Implement encoding in conversion pipeline.
  return fpromise::make_error_promise(fuchsia::media2::ConnectionError::NOT_SUPPORTED);
}

[[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError>
AudioConversionPipeline::BuildForTranscode(
    zx::eventpair buffer_collection_token,
    fidl::InterfaceRequest<fuchsia::media2::StreamSink> request) {
  return BuildForDecode(std::move(buffer_collection_token), std::move(request))
      .and_then([this]() mutable {
        FX_CHECK(!compression_);

        auto buffer_provider =
            service_provider_.ConnectToService<fuchsia::media2::BufferProvider>();
        auto [output_token, input_token] = CreateBufferCollection(*buffer_provider);
        fuchsia::media2::StreamSinkHandle handle;
        auto request = handle.NewRequest();

        fpromise::bridge<void, fuchsia::media2::ConnectionError> bridge;
        decoder_->ConnectOutputStream(
            std::move(output_token), std::move(handle),
            [completer = std::move(bridge.completer)](
                const fuchsia::audio::Decoder_ConnectOutputStream_Result& result) mutable {
              if (result.is_err()) {
                completer.complete_error(result.err());
              } else {
                completer.complete_ok();
              }
            });

        return fpromise::join_promises(bridge.consumer.promise(),
                                       BuildForEncode(std::move(input_token), std::move(request)))
            .then([](fpromise::result<
                      std::tuple<fpromise::result<void, fuchsia::media2::ConnectionError>,
                                 fpromise::result<void, fuchsia::media2::ConnectionError>>>& result)
                      -> fpromise::result<void, fuchsia::media2::ConnectionError> {
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
      });
}

}  // namespace fmlib
