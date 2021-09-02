// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>
#include <stdint.h>
#include <stdio.h>

#include <fstream>
#include <iostream>
#include <thread>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/string_number_conversions.h"
#include "src/media/codec/examples/encode_file/encoder_client.h"

namespace {
constexpr char kHelpOption[] = "help";
constexpr char kInputOption[] = "input";
constexpr char kInputWidthOption[] = "input-width";
constexpr char kInputHeightOption[] = "input-height";
constexpr char kInputFramesOption[] = "input-frames";
constexpr char kInputFormatOption[] = "input-format";
constexpr char kOutputOption[] = "output";
constexpr char kEncoderBitrateOption[] = "bitrate";
constexpr char kEncoderFramerateOption[] = "framerate";
constexpr char kEncoderCodecOption[] = "codec";
constexpr char kEncoderGopSizeOption[] = "gop";
constexpr char kDefaultInputFrames[] = "0";
constexpr char kDefaultInputFormat[] = "NV12";
constexpr char kDefaultOutputFile[] = "/tmp/out.h264";
constexpr char kDefaultEncoderBitrate[] = "1000000";
constexpr char kDefaultEncoderFramerate[] = "24";
constexpr char kDefaultEncoderCodec[] = "h264";
constexpr char kDefaultEncoderGop[] = "30";
constexpr char kH264[] = "h264";
constexpr char kH265[] = "h265";
constexpr char kNV12[] = "NV12";
constexpr char kI420[] = "I420";
}  // namespace

static void Usage(const fxl::CommandLine& command_line) {
  printf("\nUsage: %s [options]\n", command_line.argv0().c_str());
  printf("Open an input file, encode it, and write output to a file\n");
  printf("\nValid options:\n");
  printf(
      "  --%s=<filename>\tRequired. The input file to read from. Should contain raw NV12 or I420 "
      "video "
      "frames.\n",
      kInputOption);
  printf("  --%s=<width>\tRequired. The input width in pixels.\n", kInputWidthOption);
  printf("  --%s=<height>\tRequired. The input height in pixels.\n", kInputHeightOption);
  printf("\n    By default will encode all frames in input file\n");
  printf("  --%s=<frames>\tThe number of frames to encode from input file\n", kInputFramesOption);
  printf("\n    By default will write to %s\n", kDefaultOutputFile);
  printf("  --%s=<format>\tThe raw pixel format of the input. Can be NV12 or I420.\n",
         kInputFormatOption);
  printf("\n    By default will select %s\n", kDefaultInputFormat);
  printf("  --%s=<filename>\tThe output file to write encoded video to\n", kOutputOption);
  printf("\n    By default will select encoded bitrate of %s\n", kDefaultEncoderBitrate);
  printf("  --%s=<bitrate>\tTarget encoded bitrate\n", kEncoderBitrateOption);
  printf("\n    By default will select encoded framerate of %s\n", kDefaultEncoderFramerate);
  printf("  --%s=<framerate>\tTarget encoded framerate\n", kEncoderFramerateOption);
  printf("\n    By default will select %s\n", kDefaultEncoderCodec);
  printf("  --%s=<codec>\tWhich codec to encode with. Can be h264 or h265.\n", kEncoderCodecOption);
  printf("\n    By default will select encoded GOP size of %s\n", kDefaultEncoderGop);
  printf("  --%s=<gop>\tThe number of frames between key frames\n", kEncoderGopSizeOption);
}

int main(int argc, char* argv[]) {
  std::ofstream out_file;
  std::ifstream in_file;
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (command_line.HasOption(kHelpOption)) {
    Usage(command_line);
    return 0;
  }

  std::string in_filename, in_width_str, in_height_str;

  if (!command_line.GetOptionValue(kInputOption, &in_filename)) {
    std::cerr << "Input filename required" << std::endl;
    Usage(command_line);
    return EXIT_FAILURE;
  }

  if (!command_line.GetOptionValue(kInputWidthOption, &in_width_str)) {
    std::cerr << "Input width required" << std::endl;
    Usage(command_line);
    return EXIT_FAILURE;
  }

  if (!command_line.GetOptionValue(kInputHeightOption, &in_height_str)) {
    std::cerr << "Input height required" << std::endl;
    Usage(command_line);
    return EXIT_FAILURE;
  }

  auto input_frames =
      command_line.GetOptionValueWithDefault(kInputFramesOption, kDefaultInputFrames);
  auto format_str = command_line.GetOptionValueWithDefault(kInputFormatOption, kDefaultInputFormat);
  auto out_filename = command_line.GetOptionValueWithDefault(kOutputOption, kDefaultOutputFile);
  auto bitrate_str =
      command_line.GetOptionValueWithDefault(kEncoderBitrateOption, kDefaultEncoderBitrate);
  auto framerate_str =
      command_line.GetOptionValueWithDefault(kEncoderFramerateOption, kDefaultEncoderFramerate);
  auto codec_str =
      command_line.GetOptionValueWithDefault(kEncoderCodecOption, kDefaultEncoderCodec);
  auto gop_str = command_line.GetOptionValueWithDefault(kEncoderGopSizeOption, kDefaultEncoderGop);

  uint32_t bitrate = fxl::StringToNumber<uint32_t>(bitrate_str);
  uint32_t framerate = fxl::StringToNumber<uint32_t>(framerate_str);
  uint32_t gop_size = fxl::StringToNumber<uint32_t>(gop_str);
  uint32_t input_width = fxl::StringToNumber<uint32_t>(in_width_str);
  uint32_t input_height = fxl::StringToNumber<uint32_t>(in_height_str);
  uint32_t input_frame_limit = fxl::StringToNumber<uint32_t>(input_frames);

  if (format_str != kNV12 && format_str != kI420) {
    std::cerr << "Invalid input format" << std::endl;
    Usage(command_line);
    return EXIT_FAILURE;
  }

  if (codec_str != kH264 && codec_str != kH265) {
    std::cerr << "Invalid codec" << std::endl;
    Usage(command_line);
    return EXIT_FAILURE;
  }

  fuchsia::sysmem::PixelFormatType input_format;

  if (format_str == kNV12) {
    input_format = fuchsia::sysmem::PixelFormatType::NV12;
  } else if (format_str == kI420) {
    input_format = fuchsia::sysmem::PixelFormatType::I420;
  }

  std::string mime_type = "video/";
  mime_type.append(codec_str);

  // NV12 and I420 frame size
  size_t frame_size = input_width * input_height * 3 / 2;

  in_file.open(in_filename, std::ios::in | std::ios::binary);
  if (!in_file.is_open()) {
    std::cerr << "Failed to open input file" << std::endl;
    return EXIT_FAILURE;
  }

  out_file.open(out_filename, std::ios::out | std::ios::binary | std::ios::trunc);

  if (!out_file.is_open()) {
    std::cerr << "Failed to open output file" << std::endl;
    return EXIT_FAILURE;
  }
  std::cout << "Encoding " << in_filename << " to " << out_filename << std::endl;

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  fuchsia::sysmem::AllocatorHandle allocator;
  fuchsia::mediacodec::CodecFactoryHandle codec_factory;

  auto status = context->svc()->Connect(allocator.NewRequest());
  if (status != ZX_OK) {
    std::cerr << "Failed to request allocator service" << std::endl;
    return EXIT_FAILURE;
  }

  status = context->svc()->Connect(codec_factory.NewRequest());
  if (status != ZX_OK) {
    std::cerr << "Failed to request codec factory service" << std::endl;
    return EXIT_FAILURE;
  }

  status = context->svc()->Connect(allocator.NewRequest());
  if (status != ZX_OK) {
    std::cerr << "Failed to request allocator service" << std::endl;
    return EXIT_FAILURE;
  }

  auto encoder_result = EncoderClient::Create(std::move(codec_factory), std::move(allocator),
                                              bitrate, gop_size, mime_type);
  if (encoder_result.is_error()) {
    std::cerr << "Failed to create encoder client" << std::endl;
    return EXIT_FAILURE;
  }

  auto encoder = encoder_result.take_value();
  size_t bytes_written = 0;
  size_t frames_written = 0;

  // TODO(afoxley) add support for non-equal display and coded dimensions
  fuchsia::sysmem::ImageFormat_2 image_format = {
      .pixel_format =
          {
              .type = input_format,
          },
      .coded_width = input_width,
      .coded_height = input_height,
      .bytes_per_row = input_width,
      .display_width = input_width,
      .display_height = input_height,
      .color_space = fuchsia::sysmem::ColorSpace{.type = fuchsia::sysmem::ColorSpaceType::REC709},
  };

  encoder->Start(std::move(image_format), framerate);

  EncoderClient::OutputPacketHandler output_packet_handler = [&out_file, &bytes_written](
                                                                 uint8_t* buffer, size_t len) {
    bytes_written += len;
    out_file.write(reinterpret_cast<char*>(buffer), len);
  };

  encoder->SetOutputPacketHandler(std::move(output_packet_handler));

  EncoderClient::InputBufferReadyHandler input_buffer_ready_handler =
      [&in_file, &frames_written, frame_size, input_frame_limit](uint8_t* buffer, size_t size) {
        if (input_frame_limit > 0 && frames_written >= input_frame_limit) {
          return 0UL;
        }

        if (size < frame_size) {
          std::cerr << "Buffer too small";
          return 0UL;
        }

        in_file.read(reinterpret_cast<char*>(buffer), frame_size);

        if (!in_file) {
          // eof or error
          return 0UL;
        }

        frames_written++;

        return frame_size;
      };

  encoder->SetInputBufferReadyHandler(std::move(input_buffer_ready_handler));

  EncoderClient::OutputEndOfStreamHandler output_end_of_stream_handler = [&frames_written,
                                                                          &bytes_written, &loop]() {
    std::cout << "Encoded " << frames_written << " frames in " << bytes_written << " bytes"
              << std::endl;
    loop.Quit();
  };

  encoder->SetOutputEndOfStreamHandler(std::move(output_end_of_stream_handler));

  loop.Run();

  return 0;
}
