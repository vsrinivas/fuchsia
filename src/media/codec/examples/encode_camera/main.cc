// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera3/cpp/fidl.h>
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
#include "src/media/codec/examples/encode_camera/camera_client.h"
#include "src/media/codec/examples/encode_camera/encoder_client.h"

namespace {
constexpr char kHelpOption[] = "help";
constexpr char kOutputOption[] = "output";
constexpr char kDurationOption[] = "duration";
constexpr char kCameraConfigOption[] = "camera-config";
constexpr char kCameraStreamOption[] = "camera-stream";
constexpr char kCameraListOption[] = "camera-list";
constexpr char kEncoderBitrateOption[] = "encoder-bitrate";
constexpr char kEncoderCodecOption[] = "encoder-codec";
constexpr char kEncoderGopSizeOption[] = "encoder-gop";
constexpr char kDefaultOutputFile[] = "/tmp/out.h264";
constexpr char kDefaultDuration[] = "30";
constexpr char kDefaultCameraConfiguration[] = "1";
constexpr char kDefaultCameraStream[] = "1";
constexpr char kDefaultEncoderBitrate[] = "700000";
constexpr char kDefaultEncoderCodec[] = "h264";
constexpr char kDefaultEncoderGop[] = "30";
constexpr char kH264[] = "h264";
constexpr char kH265[] = "h265";
}  // namespace

static void Usage(const fxl::CommandLine& command_line) {
  printf("\nUsage: %s [options]\n", command_line.argv0().c_str());
  printf("Open a camera stream, encode it, and write to a file for a specified duration\n");
  printf("\nValid options:\n");
  printf("\n    By default will write to %s\n", kDefaultOutputFile);
  printf("  --%s=<filename>\tThe output file to write encoded video to\n", kOutputOption);
  printf("\n    By default will capture for %s seconds\n", kDefaultDuration);
  printf("  --%s=<duration>\tDuration in seconds to capture\n", kDurationOption);
  printf("\n    By default will select configuration %s\n", kDefaultCameraConfiguration);
  printf("  --%s=<configuration index>\tIndex of camera configuration to use\n",
         kCameraConfigOption);
  printf("\n    By default will select stream %s\n", kDefaultCameraStream);
  printf("  --%s=<stream index>\tIndex of stream in current configuration to open\n",
         kCameraStreamOption);
  printf("  --%s\t Print camera streams and exit\n", kCameraListOption);
  printf("\n    By default will select encoded bitrate of %s\n", kDefaultEncoderBitrate);
  printf("  --%s=<bitrate>\tTarget encoded bitrate\n", kEncoderBitrateOption);
  printf("\n    By default will select encoded GOP size of %s\n", kDefaultEncoderGop);
  printf("  --%s=<codec>\tWhich codec to encode with. Can be h264 or h265.\n", kEncoderCodecOption);
  printf("\n    By default will select %s\n", kDefaultEncoderCodec);
  printf("  --%s=<gop>\tThe number of frames between key frames\n", kEncoderGopSizeOption);
}

int main(int argc, char* argv[]) {
  std::ofstream out_file;
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (command_line.HasOption(kHelpOption)) {
    Usage(command_line);
    return 0;
  }

  auto out_filename = command_line.GetOptionValueWithDefault(kOutputOption, kDefaultOutputFile);
  auto duration_str = command_line.GetOptionValueWithDefault(kDurationOption, kDefaultDuration);
  auto config_str =
      command_line.GetOptionValueWithDefault(kCameraConfigOption, kDefaultCameraConfiguration);
  auto stream_str =
      command_line.GetOptionValueWithDefault(kCameraStreamOption, kDefaultCameraStream);
  auto bitrate_str =
      command_line.GetOptionValueWithDefault(kEncoderBitrateOption, kDefaultEncoderBitrate);
  auto codec_str =
      command_line.GetOptionValueWithDefault(kEncoderCodecOption, kDefaultEncoderCodec);
  auto gop_str = command_line.GetOptionValueWithDefault(kEncoderGopSizeOption, kDefaultEncoderGop);
  bool list_camera = command_line.HasOption(kCameraListOption);

  const zx::duration duration = zx::sec(fxl::StringToNumber<uint32_t>(duration_str));
  uint32_t config = fxl::StringToNumber<uint32_t>(config_str);
  uint32_t stream = fxl::StringToNumber<uint32_t>(stream_str);
  uint32_t bitrate = fxl::StringToNumber<uint32_t>(bitrate_str);
  uint32_t gop_size = fxl::StringToNumber<uint32_t>(gop_str);

  if (codec_str != kH264 && codec_str != kH265) {
    std::cerr << "Invalid codec" << std::endl;
    Usage(command_line);
    return EXIT_FAILURE;
  }

  std::string mime_type = "video/";
  mime_type.append(codec_str);

  if (!duration.get()) {
    std::cerr << "Invalid duration" << std::endl;
    Usage(command_line);
    return EXIT_FAILURE;
  }

  if (!list_camera) {
    out_file.open(out_filename, std::ios::out | std::ios::binary | std::ios::trunc);

    if (!out_file.is_open()) {
      std::cerr << "Failed to open output file" << std::endl;
      return EXIT_FAILURE;
    }
    std::cout << "Writing to " << out_filename << " for " << duration_str << " seconds "
              << std::endl;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  fuchsia::sysmem::AllocatorHandle allocator;
  fuchsia::camera3::DeviceWatcherHandle watcher;
  fuchsia::mediacodec::CodecFactoryHandle codec_factory;

  auto status = context->svc()->Connect(watcher.NewRequest());
  if (status != ZX_OK) {
    std::cerr << "Failed to request device watcher service" << std::endl;
    return EXIT_FAILURE;
  }

  status = context->svc()->Connect(allocator.NewRequest());
  if (status != ZX_OK) {
    std::cerr << "Failed to request allocator service" << std::endl;
    return EXIT_FAILURE;
  }

  auto camera_result =
      CameraClient::Create(std::move(watcher), std::move(allocator), list_camera, config, stream);
  if (camera_result.is_error()) {
    std::cerr << "Failed to create camera client" << std::endl;
    return EXIT_FAILURE;
  }

  auto camera = camera_result.take_value();

  allocator = nullptr;

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

  CameraClient::AddCollectionHandler add_collection_handler =
      [&encoder](fuchsia::sysmem::BufferCollectionTokenHandle token,
                 fuchsia::sysmem::ImageFormat_2 image_format,
                 fuchsia::camera3::FrameRate frame_rate) -> uint32_t {
    encoder->Start(std::move(token), std::move(image_format), frame_rate.numerator);
    return 0;
  };

  CameraClient::RemoveCollectionHandler remove_collection_handler = [](uint32_t id) {};

  CameraClient::ShowBufferHandler show_buffer_handler =
      [&encoder, &frames_written](uint32_t collection_id, uint32_t buffer_index,
                                  zx::eventpair release_fence) {
        frames_written++;
        encoder->QueueInputPacket(buffer_index, std::move(release_fence));
      };

  CameraClient::MuteStateHandler mute_handler = [](bool muted) {};

  EncoderClient::OutputPacketHandler output_packet_handler = [&out_file, &bytes_written](
                                                                 uint8_t* buffer, size_t len) {
    bytes_written += len;
    out_file.write(reinterpret_cast<char*>(buffer), len);
  };

  camera->SetHandlers(std::move(add_collection_handler), std::move(remove_collection_handler),
                      std::move(show_buffer_handler), std::move(mute_handler));

  encoder->SetOutputPacketHandler(std::move(output_packet_handler));

  async::PostDelayedTask(
      loop.dispatcher(),
      [&loop, &bytes_written, &frames_written]() {
        std::cout << "Encoded " << frames_written << " frames in " << bytes_written << " bytes"
                  << std::endl;
        loop.Quit();
      },
      duration);

  loop.Run();

  return 0;
}
