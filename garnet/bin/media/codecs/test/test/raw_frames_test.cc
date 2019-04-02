// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <openssl/sha.h>

#include "../raw_frames.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <lib/media/test/frame_sink.h>

constexpr char kInputFilePath[] = "/pkg/data/bear_320x192_40frames.yuv";
constexpr char kGoldenSha[SHA256_DIGEST_LENGTH * 2 + 1] =
    "67fdc1fed9bfbf9d1852137ba51bbda661fbf3483f5f47a553a44895de76de98";

// TODO(turnage): Unify media hashing functions in test library.
void SHA256_Update_VideoPlane(SHA256_CTX* sha256_ctx, uint8_t* start,
                              uint32_t width, uint32_t stride,
                              uint32_t height) {
  uint8_t* src = start;
  for (uint32_t row = 0; row < height; ++row) {
    SHA256_Update(sha256_ctx, src, width);
    src += stride;
  }
}

void SHA256_Char_Digest(SHA256_CTX* sha256_ctx, char* digest_str) {
  uint8_t digest[SHA256_DIGEST_LENGTH];
  SHA256_Final(digest, sha256_ctx);
  for (uint8_t byte : digest) {
    // Writes the terminating 0 each time, returns 2 each time.
    digest_str += snprintf(digest_str, 3, "%02x", byte);
  }
}

int HashFrames(RawFrames&& raw_frames) {
  SHA256_CTX sha256_ctx;
  SHA256_Init(&sha256_ctx);

  size_t i = 0;
  std::optional<RawFrames::Image> frame;
  while ((frame = raw_frames.Frame(i++))) {
    // Y
    SHA256_Update_VideoPlane(
        &sha256_ctx,
        /*start=*/frame->image_start + frame->format.primary_start_offset,
        frame->format.primary_width_pixels,
        frame->format.primary_line_stride_bytes,
        frame->format.primary_height_pixels);

    // V
    SHA256_Update_VideoPlane(
        &sha256_ctx,
        /*start=*/frame->image_start + frame->format.secondary_start_offset,
        frame->format.secondary_width_pixels,
        frame->format.secondary_line_stride_bytes,
        frame->format.secondary_height_pixels);

    // U
    SHA256_Update_VideoPlane(
        &sha256_ctx,
        /*start=*/frame->image_start + frame->format.tertiary_start_offset,
        frame->format.secondary_width_pixels,
        frame->format.secondary_line_stride_bytes,
        frame->format.secondary_height_pixels);
  }

  char digest_str[SHA256_DIGEST_LENGTH * 2 + 1];
  SHA256_Char_Digest(&sha256_ctx, digest_str);
  int delta = memcmp(digest_str, kGoldenSha, SHA256_DIGEST_LENGTH);
  if (delta) {
    fprintf(stderr, "The golden sha is: %s\n", kGoldenSha);
    fprintf(stderr, "The video sha is: %s\n", digest_str);
  }

  return delta == 0 ? 0 : -1;
}

int SendFramesToScenic(RawFrames&& raw_frames) {
  async::Loop main_loop(&kAsyncLoopConfigAttachToThread);
  std::unique_ptr<component::StartupContext> startup_context =
      component::StartupContext::CreateFromStartupInfo();

  auto send_frames = [&main_loop, &raw_frames](FrameSink* frame_sink) {
    std::optional<RawFrames::Image> frame;
    size_t frames_sent = 0;
    while ((frame = raw_frames.Frame(frames_sent++))) {
      auto format = std::make_shared<fuchsia::media::StreamOutputFormat>();
      format->mutable_format_details()
          ->mutable_domain()
          ->video()
          .set_uncompressed(std::move(frame->format));
      frame_sink->PutFrame(frames_sent, frame->vmo, frame->vmo_offset, format,
                           [] {});
    }

    frame_sink->PutEndOfStreamThenWaitForFramesReturnedAsync(
        [&main_loop] { main_loop.Shutdown(); });
  };

  auto frame_sink = FrameSink::Create(startup_context.get(), &main_loop, 24.0,
                                      std::move(send_frames));
  if (!frame_sink) {
    FXL_LOG(FATAL) << "Failed to create FrameSink.";
  }

  main_loop.Run();

  return 0;
}

// To see frames manually, run
/*
  fx shell present_view \
  fuchsia-pkg://fuchsia.com/raw_frames_test#meta/raw_frames_test.cmx \
  --imagepipe
*/
// Otherwise, the frames will be compared automatically against a sha.
int main(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line)) {
    FXL_LOG(FATAL) << "Failed to parse log settings.";
  }

  auto raw_frames =
      RawFrames::FromI420File(kInputFilePath, {
                                                  .width = 320,
                                                  .height = 192,
                                                  .stride = 320,
                                                  .frame_alignment = 1024 * 4,
                                              });
  if (!raw_frames) {
    FXL_LOG(FATAL) << "Failed to parse raw frames from file.";
  }

  if (command_line.HasOption("imagepipe")) {
    return SendFramesToScenic(std::move(*raw_frames));
  }

  return HashFrames(std::move(*raw_frames));
}
