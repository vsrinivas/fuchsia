// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "video_decoder_fuzzer_test.h"

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/media/codec_impl/fourcc.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>
#include <lib/syslog/logger.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <map>
#include <random>
#include <set>

#include "../in_stream_file.h"
#include "../in_stream_peeker.h"
#include "../input_copier.h"
#include "../use_video_decoder.h"
#include "../util.h"
#include "src/lib/fxl/logging.h"

namespace {

// 8MiB max peek is essentially for h264 streams.  VP9 streams don't need to
// scan for start codes so won't peek anywhere near this much.
constexpr uint32_t kMaxPeekBytes = 8 * 1024 * 1024;

}  // namespace

class MutatingInputCopier : public InputCopier {
 public:
  // |modified_instance| determines which call into DecryptVideo has data modified. In general
  // DecryptVideo is called per-access-unit, so setting modified_instance will normally change which
  // frame is modified.
  MutatingInputCopier(int modified_instance, int modified_offset, uint8_t modified_value)
      : modified_instance_(modified_instance),
        modified_offset_(modified_offset),
        modified_value_(modified_value) {}

  uint32_t PaddingLength() const override { return 0; }
  int DecryptVideo(void* data, uint32_t data_len, const zx::vmo& vmo) override {
    vmo.write(data, 0, data_len);
    if (modified_instance_-- == 0) {
      if (modified_offset_ < static_cast<int>(data_len)) {
        fprintf(stderr, "Modifying stream byte offset: %d\n", total_read_bytes_ + modified_offset_);
        vmo.write(&modified_value_, modified_offset_, 1);
      } else {
        fprintf(stderr, "Offset out of range, not modifying stream");
      }
    }
    total_read_bytes_ += data_len;
    return 0;
  }

 private:
  int modified_instance_;
  int modified_offset_;
  uint8_t modified_value_;
  uint32_t total_read_bytes_ = 0;
};

int run_fuzzer_test_instance(std::string input_file_path, UseVideoDecoderFunction use_video_decoder,
                             int instance, int loc, uint8_t random_value) {
  async::Loop fidl_loop(&kAsyncLoopConfigAttachToCurrentThread);
  thrd_t fidl_thread;
  ZX_ASSERT(ZX_OK == fidl_loop.StartThread("FIDL_thread", &fidl_thread));
  std::unique_ptr<sys::ComponentContext> component_context = sys::ComponentContext::Create();

  printf("Decoding test file %s\n", input_file_path.c_str());

  auto in_stream_file = std::make_unique<InStreamFile>(&fidl_loop, fidl_thread,
                                                       component_context.get(), input_file_path);
  auto in_stream_peeker = std::make_unique<InStreamPeeker>(
      &fidl_loop, fidl_thread, component_context.get(), std::move(in_stream_file), kMaxPeekBytes);

  std::vector<std::pair<bool, uint64_t>> timestamps;

  uint32_t frame_index = 0;
  EmitFrame emit_frame = [&timestamps, &frame_index](
                             uint8_t* i420_data, uint32_t width, uint32_t height, uint32_t stride,
                             bool has_timestamp_ish, uint64_t timestamp_ish) {
    VLOGF("emit_frame frame_index: %u", frame_index);
    ZX_ASSERT_MSG(width % 2 == 0, "odd width not yet handled");
    ZX_ASSERT_MSG(width == stride, "stride != width not yet handled");
    timestamps.push_back({has_timestamp_ish, timestamp_ish});
    frame_index++;
  };

  fuchsia::mediacodec::CodecFactoryPtr codec_factory;
  codec_factory.set_error_handler(
      [](zx_status_t status) { FXL_PLOG(FATAL, status) << "codec_factory failed - unexpected"; });
  component_context->svc()->Connect<fuchsia::mediacodec::CodecFactory>(codec_factory.NewRequest());
  fuchsia::sysmem::AllocatorPtr sysmem;
  sysmem.set_error_handler(
      [](zx_status_t status) { FXL_PLOG(FATAL, status) << "sysmem failed - unexpected"; });
  component_context->svc()->Connect<fuchsia::sysmem::Allocator>(sysmem.NewRequest());

  auto input_copier = std::make_unique<MutatingInputCopier>(instance, loc, random_value);

  use_video_decoder(&fidl_loop, fidl_thread, std::move(codec_factory), std::move(sysmem),
                    in_stream_peeker.get(), input_copier.get(), /*min_output_buffer_size=*/0,
                    /*min_output_buffer_count=*/0, /*is_secure_output=*/false,
                    /*is_secure_input=*/false, /*lax_mode=*/true, nullptr, std::move(emit_frame));

  fidl_loop.Quit();
  fidl_loop.JoinThreads();
  component_context.reset();
  fidl_loop.Shutdown();
  fprintf(stderr, "Fuzzed, got frame count: %d\n", frame_index);

  return 0;
}

int video_fuzzer_test(std::string input_file_path, UseVideoDecoderFunction use_video_decoder,
                      uint32_t iteration_count, fxl::CommandLine command_line) {
  syslog::LogSettings settings = {.severity = FX_LOG_INFO, .fd = STDERR_FILENO};
  zx_status_t status = syslog::InitLogger(settings, {"video_decoder_fuzzer_test"});
  ZX_ASSERT(status == ZX_OK);
  fx_logger_t* logger = fx_log_get_logger();
  ZX_ASSERT(logger);
  // Default seed.
  std::mt19937 gen;

  // Fuzz the first 31 access units.
  std::uniform_int_distribution<> instance_dist(0, 30);
  // Fuzz the first 101 bytes of the access unit because that's where the headers are so they're
  // more likely to give interesting results.
  std::uniform_int_distribution<> offset_dist(0, 100);
  std::uniform_int_distribution<> value_dis(0, 255);
  uint32_t start_iteration = atoi(command_line.GetOptionValueWithDefault("start", "0").c_str());
  std::string iteration_limit;
  if (command_line.GetOptionValue("iteration-limit", &iteration_limit)) {
    iteration_count = atoi(iteration_limit.c_str());
  }
  for (uint32_t i = 0; i < iteration_count; i++) {
    int random_instance = instance_dist(gen);
    int random_location = offset_dist(gen);
    uint8_t random_value = value_dis(gen);
    if (i < start_iteration)
      continue;

    fprintf(stderr, "%d: Trying instance %d location %d value %d\n", i, random_instance,
            random_location, random_value);
    if (run_fuzzer_test_instance(input_file_path, use_video_decoder, random_instance,
                                 random_location, random_value) < 0) {
      fprintf(stderr, "Fuzz instance returned error\n");

      return -1;
    }
  }
  return 0;
}
