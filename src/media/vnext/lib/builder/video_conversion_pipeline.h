// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_VNEXT_LIB_BUILDER_VIDEO_CONVERSION_PIPELINE_H_
#define SRC_MEDIA_VNEXT_LIB_BUILDER_VIDEO_CONVERSION_PIPELINE_H_

#include <fuchsia/media2/cpp/fidl.h>
#include <fuchsia/video/cpp/fidl.h>
#include <lib/fpromise/scope.h>

#include "src/media/vnext/lib/hosting/service_provider.h"
#include "src/media/vnext/lib/threads/thread.h"

namespace fmlib {

// This class creates conversion pipelines that decode, encode or transcode video streams as needed.
// TODO(dalesat): Only decoding is currently implemented.
//
// The static |Create| methods determine whether conversion is required for the given parameters. If
// conversion is not required, they return a null unique pointer. If conversion is required, they
// return a unique pointer to a valid |VideoConversionPipeline|. No attempt is made to actually
// create and connect converters until the |ConnectInputStream| method is called.
//
// |VideoConversionPipeline| instances are not thread-safe.
//
// TODO(dalesat): This could maybe be merged with AudioConversionPipeline using templates.
class VideoConversionPipeline {
 public:
  // Returns a unique pointer to a |VideoConversionPipeline| unless the parameters indicate no need
  // for conversion, in which case this method returns a null unique pointer. |format| is the format
  // of both the input and the output of the pipeline. |input_compression| indicates the compression
  // applied to the pipeline's input stream, nullptr indicating no compression.
  // |output_compression_type| indicates the desired compression to be applied to the pipeline's
  // output stream, nullptr indicating no compression. |service_provider| must remain valid
  // throughout the lifetime of the returned pipeline.
  static std::unique_ptr<VideoConversionPipeline> Create(
      const fuchsia::mediastreams::VideoFormat& format,
      const fuchsia::mediastreams::CompressionPtr& input_compression,
      const std::unique_ptr<std::string>& output_compression_type,
      ServiceProvider& service_provider);

  // Returns a unique pointer to a |VideoConversionPipeline| unless the parameters indicate no need
  // for conversion, in which case this method returns a null unique pointer. |format| is the format
  // of both the input and the output of the pipeline. |input_compression| indicates the compression
  // applied to the pipeline's input stream, nullptr indicating no compression.
  // |output_supported_compression_types| indicates the range of desired compression types, one of
  // which is to be applied to the pipeline's output stream. If |output_supports_uncompressed| is
  // true, the output stream may also be uncompression. |service_provider| must remain valid
  // throughout the lifetime of the returned pipeline.
  static std::unique_ptr<VideoConversionPipeline> Create(
      const fuchsia::mediastreams::VideoFormat& format,
      const fuchsia::mediastreams::CompressionPtr& input_compression,
      const std::vector<std::string>& output_supported_compression_types,
      bool output_supports_uncompressed, ServiceProvider& service_provider);

  // Constructs an |VideoConversionPipeline|. Use a |Create| method instead.
  VideoConversionPipeline(fuchsia::mediastreams::VideoFormat format,
                          fuchsia::mediastreams::CompressionPtr input_compression,
                          std::vector<std::string> output_supported_compression_types,
                          bool output_supports_uncompressed, ServiceProvider& service_provider)
      : format_(std::move(format)),
        compression_(std::move(input_compression)),
        output_supported_compression_types_(std::move(output_supported_compression_types)),
        output_supports_uncompressed_(output_supports_uncompressed),
        service_provider_(service_provider) {}

  ~VideoConversionPipeline() = default;

  // Disallow copy, assign and move.
  VideoConversionPipeline(const VideoConversionPipeline&) = delete;
  VideoConversionPipeline& operator=(const VideoConversionPipeline&) = delete;
  VideoConversionPipeline(VideoConversionPipeline&&) = delete;
  VideoConversionPipeline& operator=(VideoConversionPipeline&&) = delete;

  // Starts connecting the input stream of this pipeline and returns a promise that completes when
  // the output is available to connect.
  [[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectInputStream(
      zx::eventpair buffer_collection_token,
      fuchsia::media2::PacketTimestampUnitsPtr timestamp_units,
      fidl::InterfaceRequest<fuchsia::media2::StreamSink> request);

  // Starts connectiing the output stream of this pipeline and returns a promise that completes when
  // the the output is connected. This method must not be called until the promise returned by
  // |ConnectInputStream| completes.
  [[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError> ConnectOutputStream(
      zx::eventpair buffer_collection_token, fuchsia::media2::StreamSinkHandle handle);

  // Returns the format of the output stream. This method must not be called until the promise
  // returned by |ConnectInputStream| completes.
  fuchsia::mediastreams::VideoFormat output_format() const {
    FX_CHECK(output_stream_available_);
    return fidl::Clone(format_);
  }

  // Returns the compression applied to the output stream. This method must not be called until the
  // promise returned by |ConnectInputStream| completes.
  fuchsia::mediastreams::CompressionPtr output_compression() const {
    FX_CHECK(output_stream_available_);
    return fidl::Clone(compression_);
  }

 private:
  [[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError> BuildForDecode(
      zx::eventpair buffer_collection_token,
      fidl::InterfaceRequest<fuchsia::media2::StreamSink> request);

  [[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError> BuildForEncode(
      zx::eventpair buffer_collection_token,
      fidl::InterfaceRequest<fuchsia::media2::StreamSink> request);

  [[nodiscard]] fpromise::promise<void, fuchsia::media2::ConnectionError> BuildForTranscode(
      zx::eventpair buffer_collection_token,
      fidl::InterfaceRequest<fuchsia::media2::StreamSink> request);

  fuchsia::mediastreams::VideoFormat format_;
  fuchsia::mediastreams::CompressionPtr compression_;
  fuchsia::media2::PacketTimestampUnitsPtr timestamp_units_;
  std::vector<std::string> output_supported_compression_types_;
  bool output_supports_uncompressed_;
  ServiceProvider& service_provider_;
  bool output_stream_available_ = false;

  fuchsia::video::DecoderPtr decoder_;
};

}  // namespace fmlib

#endif  // SRC_MEDIA_VNEXT_LIB_BUILDER_VIDEO_CONVERSION_PIPELINE_H_
