// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <byteswap.h>
#include <lib/syslog/cpp/log_settings.h>
#include <zircon/compiler.h>

#include <gtest/gtest.h>

#include "amlogic-video.h"
#include "bear_h264_hashes.h"
#include "h264_multi_decoder.h"
#include "h264_utils.h"
#include "lib/async-loop/default.h"
#include "macros.h"
#include "pts_manager.h"
#include "test_frame_allocator.h"
#include "tests/integration/test_25fps_h264_hashes.h"
#include "tests/test_support.h"
#include "vdec1.h"
#include "video_frame_helpers.h"

namespace amlogic_decoder {
namespace test {

class H264TestFrameDataProvider final : public H264MultiDecoder::FrameDataProvider {
 public:
  H264TestFrameDataProvider(AmlogicVideo* video)
      : video_(video), async_loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
    ZX_ASSERT(ZX_OK == async_loop_.StartThread("async_loop_"));
  }
  void set_decoder(H264MultiDecoder* decoder) { decoder_ = decoder; }

  void AppendFrameData(std::vector<std::vector<uint8_t>> frame_data) {
    frame_data_.insert(frame_data_.end(), frame_data.begin(), frame_data.end());
  }

  void set_async_reset_handler(fit::closure handler) { async_reset_handler_ = std::move(handler); }

  std::optional<H264MultiDecoder::DataInput> ReadMoreInputData() override {
    H264MultiDecoder::DataInput result;
    if (frame_data_.empty()) {
      return std::nullopt;
    }
    result.data = std::move(frame_data_.front());
    result.length = result.data.size();
    frame_data_.pop_front();
    uint32_t nal_unit_type = GetNalUnitType(result.data);
    if (nal_unit_type == 1 || nal_unit_type == 5) {
      // Only assign PTS for slices, to try to avoid jumps.
      result.pts = next_pts_++;
    }
    return result;
  }
  bool HasMoreInputData() override { return !frame_data_.empty(); }
  void AsyncPumpDecoder() override {
    async::PostTask(async_loop_.dispatcher(), [this] {
      std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
      decoder_->PumpOrReschedule();
    });
  }
  void AsyncResetStreamAfterCurrentFrame() override {
    EXPECT_TRUE(async_reset_handler_);
    async_reset_handler_();
  }

 private:
  AmlogicVideo* video_ = nullptr;
  H264MultiDecoder* decoder_ = nullptr;
  std::list<std::vector<uint8_t>> frame_data_;
  uint64_t next_pts_{};
  fit::closure async_reset_handler_;
  async::Loop async_loop_;
};

class FakeOwner : public AmlogicVideo::Owner {
 public:
  // AmlogicVideo::Owner implementation.
  void SetThreadProfile(zx::unowned_thread thread, ThreadRole role) const override {}
};

// Set the min logging level so every log will display.
static void SetMaxLogging() {
  syslog::LogSettings settings;
  settings.min_log_level = -10;
  syslog::SetLogSettings(settings);
}

class TestH264Multi {
 public:
  struct VideoInfo {
    const char* input_filename;
    uint8_t (*input_hashes)[SHA256_DIGEST_LENGTH];
    const char* filename;
    uint32_t coded_width;
    uint32_t coded_height;
    uint32_t display_width;
    uint32_t display_height;
    uint32_t expected_frame_count;
    bool has_sar;
  };
  static void DecodeSetStream(const VideoInfo& data, bool use_parser) {
    SetMaxLogging();
    FakeOwner owner;
    auto video = std::make_unique<AmlogicVideo>(&owner);
    ASSERT_TRUE(video);
    TestFrameAllocator frame_allocator(video.get());

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());
    H264TestFrameDataProvider frame_data_provider(video.get());
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      auto decoder = std::make_unique<H264MultiDecoder>(
          video.get(), &frame_allocator, &frame_data_provider, std::nullopt, /*is_secure=*/false);
      frame_data_provider.set_decoder(decoder.get());
      decoder->set_use_parser(use_parser);
      video->SetDefaultInstance(std::move(decoder),
                                /*hevc=*/false);
      frame_allocator.set_decoder(video->video_decoder());
    }

    frame_allocator.set_pump_function([&video]() {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      static_cast<H264MultiDecoder*>(video->video_decoder())->PumpOrReschedule();
    });
    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(use_parser, 1024 * PAGE_SIZE,
                                                   /*is_secure=*/false));
    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    std::set<uint64_t> received_pts_set;
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      frame_allocator.SetFrameReadyNotifier([&video, &frame_count, &wait_valid, &data,
                                             &received_pts_set,
                                             &frame_allocator](std::shared_ptr<VideoFrame> frame) {
        ++frame_count;
        DLOG("Got frame %d\n", frame_count);
        EXPECT_EQ(data.coded_width, frame->coded_width);
        EXPECT_EQ(data.display_width, frame->display_width);
        EXPECT_EQ(data.coded_height, frame->coded_height);
        EXPECT_EQ(data.display_height, frame->display_height);
        bool is_bear = data.input_filename == std::string("video_test_data/bear.h264");
#if DUMP_VIDEO_TO_FILE
        DumpVideoFrameToFile(frame.get(), data.filename);
#endif
        io_buffer_cache_flush_invalidate(&frame->buffer, 0, frame->stride * frame->coded_height);
        io_buffer_cache_flush_invalidate(&frame->buffer, frame->uv_plane_offset,
                                         frame->stride * frame->coded_height / 2);

        uint8_t* buf_start = static_cast<uint8_t*>(io_buffer_virt(&frame->buffer));
        if (frame_count == 1 && is_bear) {
          // Only test a small amount to try to make the output of huge failures obvious - the rest
          // can be verified through hashes.
          constexpr uint8_t kExpectedData[] = {124, 186, 230, 247, 252, 252, 252, 252, 252, 252};
          for (uint32_t i = 0; i < std::size(kExpectedData); ++i) {
            EXPECT_EQ(kExpectedData[i], buf_start[i]) << " index " << i;
          }
        }

        if (data.input_hashes) {
          uint8_t md[SHA256_DIGEST_LENGTH];
          HashFrame(frame.get(), md);
          EXPECT_EQ(0, memcmp(md, data.input_hashes[frame_count - 1], sizeof(md)))
              << "Incorrect hash for frame " << frame_count << ": " << StringifyHash(md);
        }

        EXPECT_TRUE(frame->has_pts);
        // The "pts" assigned in the TestFrameDataProvider goes in decode order, so we need to allow
        // the current one to be two less than the max previously seen.
        if (received_pts_set.size() > 0)
          EXPECT_LE(*std::prev(received_pts_set.end()), frame->pts + 2);
        EXPECT_EQ(0u, received_pts_set.count(frame->pts));
        received_pts_set.insert(frame->pts);

        EXPECT_EQ(data.has_sar, frame_allocator.has_sar());

        video->AssertVideoDecoderLockHeld();
        video->video_decoder()->ReturnFrame(frame);
        if (frame_count == data.expected_frame_count) {
          wait_valid.set_value();
        }
      });

      // Initialize must happen after InitializeStreamBuffer or else it may misparse the SPS.
      EXPECT_EQ(ZX_OK, video->video_decoder()->Initialize());
    }

    auto input_h264 = TestSupport::LoadFirmwareFile(data.input_filename);
    ASSERT_NE(nullptr, input_h264);
    video->core()->InitializeDirectInput();
    auto nal_units = SplitNalUnits(input_h264->ptr, input_h264->size);
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      frame_data_provider.AppendFrameData(std::move(nal_units));
      auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      multi_decoder->ReceivedNewInput();
    }
    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(10)));
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      multi_decoder->DumpStatus();
    }

    EXPECT_LE(data.expected_frame_count, frame_count);

    video->ClearDecoderInstance();
    video.reset();
  }

  static void DecodeUnsplit(const char* input_filename,
                            uint8_t (*input_hashes)[SHA256_DIGEST_LENGTH], const char* filename) {
    SetMaxLogging();
    FakeOwner owner;
    auto video = std::make_unique<AmlogicVideo>(&owner);
    ASSERT_TRUE(video);
    TestFrameAllocator frame_allocator(video.get());

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());
    H264TestFrameDataProvider frame_data_provider(video.get());
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      video->SetDefaultInstance(
          std::make_unique<H264MultiDecoder>(video.get(), &frame_allocator, &frame_data_provider,
                                             std::nullopt, false),
          /*hevc=*/false);
      frame_data_provider.set_decoder(static_cast<H264MultiDecoder*>(video->video_decoder()));
      frame_allocator.set_decoder(video->video_decoder());
    }

    frame_allocator.set_pump_function([&video]() {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      static_cast<H264MultiDecoder*>(video->video_decoder())->PumpOrReschedule();
    });
    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/false, 1024 * PAGE_SIZE,
                                                   /*is_secure=*/false));
    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      frame_allocator.SetFrameReadyNotifier([&video, &frame_count, &wait_valid, input_filename,
                                             filename,
                                             input_hashes](std::shared_ptr<VideoFrame> frame) {
        ++frame_count;
        DLOG("Got frame %d\n", frame_count);
        EXPECT_EQ(320u, frame->coded_width);
        EXPECT_EQ(320u, frame->display_width);
        bool is_bear = input_filename == std::string("video_test_data/bear.h264");
        if (is_bear) {
          EXPECT_EQ(192u, frame->coded_height);
          EXPECT_EQ(180u, frame->display_height);
        } else {
          EXPECT_EQ(240u, frame->coded_height);
          EXPECT_EQ(240u, frame->display_height);
        }
        (void)filename;
#if DUMP_VIDEO_TO_FILE
        DumpVideoFrameToFile(frame.get(), filename);
#endif

        uint8_t md[SHA256_DIGEST_LENGTH];
        HashFrame(frame.get(), md);
        EXPECT_EQ(0, memcmp(md, input_hashes[frame_count - 1], sizeof(md)))
            << "Incorrect hash for frame " << frame_count << ": " << StringifyHash(md);

        video->AssertVideoDecoderLockHeld();
        video->video_decoder()->ReturnFrame(frame);
        uint32_t expected_frame_count = is_bear ? 26 : 240;
        if (frame_count == expected_frame_count) {
          wait_valid.set_value();
        }
      });

      // Initialize must happen after InitializeStreamBuffer or else it may misparse the SPS.
      EXPECT_EQ(ZX_OK, video->video_decoder()->Initialize());
    }

    auto input_h264 = TestSupport::LoadFirmwareFile(input_filename);
    ASSERT_NE(nullptr, input_h264);
    video->core()->InitializeDirectInput();
    auto nal_units = SplitNalUnits(input_h264->ptr, input_h264->size);
    std::vector<std::vector<uint8_t>> full_data;
    full_data.emplace_back(
        std::vector<uint8_t>(input_h264->ptr, input_h264->ptr + input_h264->size));
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      frame_data_provider.AppendFrameData(full_data);
      auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      multi_decoder->ReceivedNewInput();
    }
    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(10)));
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      multi_decoder->DumpStatus();
    }

    // Try to make sure no more frames are decoded.
    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));

    EXPECT_EQ(28u, frame_count);

    video->ClearDecoderInstance();
    video.reset();
  }

  static void TestInitializeTwice() {
    FakeOwner owner;
    auto video = std::make_unique<AmlogicVideo>(&owner);
    ASSERT_TRUE(video);
    TestFrameAllocator frame_allocator(video.get());

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());
    H264TestFrameDataProvider frame_data_provider(video.get());

    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      video->SetDefaultInstance(
          std::make_unique<H264MultiDecoder>(video.get(), &frame_allocator, &frame_data_provider,
                                             std::nullopt, /*is_secure=*/false),
          /*hevc=*/false);
      frame_data_provider.set_decoder(static_cast<H264MultiDecoder*>(video->video_decoder()));
      frame_allocator.set_decoder(video->video_decoder());
    }
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/false, 1024 * PAGE_SIZE,
                                                   /*is_secure=*/false));
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());

      EXPECT_EQ(ZX_OK, video->video_decoder()->Initialize());
      auto* decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      void* virt_base_1 = decoder->SecondaryFirmwareVirtualAddressForTesting();

      decoder->SetSwappedOut();
      EXPECT_EQ(ZX_OK, video->video_decoder()->InitializeHardware());
      EXPECT_EQ(virt_base_1, decoder->SecondaryFirmwareVirtualAddressForTesting());
    }
    video.reset();
  }

  static void DecodeMultiInstance() {
    SetMaxLogging();
    FakeOwner owner;
    auto video = std::make_unique<AmlogicVideo>(&owner);
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());
    std::vector<std::unique_ptr<TestFrameAllocator>> clients;
    std::vector<std::unique_ptr<H264TestFrameDataProvider>> providers;
    std::vector<H264MultiDecoder*> decoder_ptrs;

    for (uint32_t i = 0; i < 2; i++) {
      auto client = std::make_unique<TestFrameAllocator>(video.get());
      auto provider = std::make_unique<H264TestFrameDataProvider>(video.get());
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      auto decoder = std::make_unique<H264MultiDecoder>(video.get(), client.get(), provider.get(),
                                                        std::nullopt, /*is_secure=*/false);
      decoder_ptrs.push_back(decoder.get());
      provider->set_decoder(decoder.get());
      client->set_decoder(decoder.get());
      client->set_pump_function([&video, &decoder_ptrs, i]() {
        std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
        decoder_ptrs[i]->PumpOrReschedule();
      });
      clients.push_back(std::move(client));
      providers.push_back(std::move(provider));
      EXPECT_EQ(ZX_OK, decoder->InitializeBuffers());
      auto decoder_instance =
          std::make_unique<DecoderInstance>(std::move(decoder), video->vdec1_core());
      StreamBuffer* buffer = decoder_instance->stream_buffer();
      video->AddNewDecoderInstance(std::move(decoder_instance));
      EXPECT_EQ(ZX_OK, video->AllocateStreamBuffer(buffer, PAGE_SIZE * 1024, std::nullopt,
                                                   /*use_parser=*/false,
                                                   /*is_secure=*/false));
    }

    struct ClientData {
      uint32_t frame_count{};
      uint32_t expected_frame_count{};
      std::promise<void> wait_valid;
      uint8_t (*input_hashes)[SHA256_DIGEST_LENGTH];
    };

    uint32_t last_client_index = UINT32_MAX;
    uint32_t context_switch_count = 0;

    std::vector<ClientData> client_data(2);
    for (uint32_t i = 0; i < 2; i++) {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      clients[i]->SetFrameReadyNotifier([&video, client_ptr = clients[i].get(), i,
                                         client_data_ptr = &client_data[i], &last_client_index,
                                         &context_switch_count](std::shared_ptr<VideoFrame> frame) {
        client_data_ptr->frame_count++;
        DLOG("Got frame %d client %d\n", client_data_ptr->frame_count, i);
        EXPECT_EQ(320u, frame->coded_width);
        EXPECT_EQ(320u, frame->display_width);
#if DUMP_VIDEO_TO_FILE
        DumpVideoFrameToFile(frame.get(), filename);
#endif

        uint8_t md[SHA256_DIGEST_LENGTH];
        HashFrame(frame.get(), md);
        EXPECT_EQ(0, memcmp(md, client_data_ptr->input_hashes[client_data_ptr->frame_count - 1],
                            sizeof(md)))
            << "Incorrect hash for frame " << client_data_ptr->frame_count << ": "
            << StringifyHash(md);
        video->AssertVideoDecoderLockHeld();
        video->video_decoder()->ReturnFrame(frame);

        if (last_client_index != i) {
          context_switch_count++;
        }
        last_client_index = i;

        if (client_data_ptr->frame_count == client_data_ptr->expected_frame_count) {
          client_data_ptr->wait_valid.set_value();
        }
      });
    }

    // Put test-25fps before bear.h264 because it's much longer and has a larger DPB so it takes
    // longer to start outputting frames. This way there will be more alternation between them if
    // everything works properly.
    std::vector<const char*> input_files{"video_test_data/test-25fps.h264",
                                         "video_test_data/bear.h264"};
    client_data[0].expected_frame_count = 240;
    client_data[0].input_hashes = test_25fps_h264_hashes;
    client_data[1].expected_frame_count = 26;
    client_data[1].input_hashes = bear_h264_hashes;
    for (uint32_t i = 0; i < input_files.size(); ++i) {
      auto input_h264 = TestSupport::LoadFirmwareFile(input_files[i]);
      ASSERT_NE(nullptr, input_h264);
      auto nal_units = SplitNalUnits(input_h264->ptr, input_h264->size);
      {
        std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
        providers[i]->AppendFrameData(std::move(nal_units));
        decoder_ptrs[i]->ReceivedNewInput();
      }
    }

    EXPECT_EQ(std::future_status::ready,
              client_data[0].wait_valid.get_future().wait_for(std::chrono::seconds(10)));
    EXPECT_EQ(std::future_status::ready,
              client_data[1].wait_valid.get_future().wait_for(std::chrono::seconds(10)));
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      multi_decoder->DumpStatus();
    }

    // A mostly-arbitrary number to ensure we don't just decode all of one video then all of
    // another.
    EXPECT_LE(5u, context_switch_count);

    for (auto& decoder : decoder_ptrs) {
      video->RemoveDecoder(decoder);
    }
    video.reset();
  }

  static void DecodeChangeConfig() {
    SetMaxLogging();
    FakeOwner owner;
    auto video = std::make_unique<AmlogicVideo>(&owner);
    ASSERT_TRUE(video);
    TestFrameAllocator frame_allocator(video.get());

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());
    H264TestFrameDataProvider frame_data_provider(video.get());
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      video->SetDefaultInstance(
          std::make_unique<H264MultiDecoder>(video.get(), &frame_allocator, &frame_data_provider,
                                             std::nullopt, /*is_secure=*/false),
          /*hevc=*/false);
      frame_data_provider.set_decoder(static_cast<H264MultiDecoder*>(video->video_decoder()));
      frame_allocator.set_decoder(video->video_decoder());
    }
    frame_allocator.set_pump_function([&video]() {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      static_cast<H264MultiDecoder*>(video->video_decoder())->PumpOrReschedule();
    });
    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/false, 1024 * PAGE_SIZE,
                                                   /*is_secure=*/false));
    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      frame_allocator.SetFrameReadyNotifier(
          [&video, &frame_count, &wait_valid](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d", frame_count);
            EXPECT_EQ(320u, frame->coded_width);
            EXPECT_EQ(320u, frame->display_width);
            constexpr uint32_t k25fpsVideoLength = 250;
            bool is_bear = frame_count > k25fpsVideoLength;
            if (is_bear) {
              EXPECT_EQ(192u, frame->coded_height);
              EXPECT_EQ(180u, frame->display_height);
            } else {
              EXPECT_EQ(240u, frame->coded_height);
              EXPECT_EQ(240u, frame->display_height);
            }
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame.get(), "/tmp/changeconfigmultih264.yuv");
#endif
            uint32_t in_video_frame_count = is_bear ? frame_count - k25fpsVideoLength : frame_count;
            auto hashes = is_bear ? bear_h264_hashes : test_25fps_h264_hashes;
            uint64_t max_size =
                is_bear ? std::size(bear_h264_hashes) : std::size(test_25fps_h264_hashes);
            if (in_video_frame_count <= max_size) {
              uint8_t md[SHA256_DIGEST_LENGTH];
              HashFrame(frame.get(), md);
              EXPECT_EQ(0, memcmp(md, hashes[in_video_frame_count - 1], sizeof(md)))
                  << "Incorrect hash for frame " << frame_count << ": " << StringifyHash(md);
            }

            video->AssertVideoDecoderLockHeld();
            video->video_decoder()->ReturnFrame(frame);
            if (frame_count == 26 + k25fpsVideoLength) {
              wait_valid.set_value();
            }
          });

      // Initialize must happen after InitializeStreamBuffer or else it may misparse the SPS.
      EXPECT_EQ(ZX_OK, video->video_decoder()->Initialize());
    }
    video->core()->InitializeDirectInput();

    std::vector<const char*> input_files{"video_test_data/test-25fps.h264",
                                         "video_test_data/bear.h264"};
    for (auto* input_filename : input_files) {
      auto input_h264 = TestSupport::LoadFirmwareFile(input_filename);
      ASSERT_NE(nullptr, input_h264);
      auto nal_units = SplitNalUnits(input_h264->ptr, input_h264->size);
      {
        std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
        frame_data_provider.AppendFrameData(std::move(nal_units));
        auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
        multi_decoder->ReceivedNewInput();
      }
    }
    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(10)));
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      multi_decoder->DumpStatus();
    }

    video->ClearDecoderInstance();
    video.reset();
  }

  static void DecodeWithEos(const char* input_filename,
                            uint8_t (*input_hashes)[SHA256_DIGEST_LENGTH], const char* filename,
                            bool early_eos) {
    SetMaxLogging();
    FakeOwner owner;
    auto video = std::make_unique<AmlogicVideo>(&owner);
    ASSERT_TRUE(video);
    TestFrameAllocator frame_allocator(video.get());

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());
    H264TestFrameDataProvider frame_data_provider(video.get());
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      video->SetDefaultInstance(
          std::make_unique<H264MultiDecoder>(video.get(), &frame_allocator, &frame_data_provider,
                                             std::nullopt, /*is_secure=*/false),
          /*hevc=*/false);
      frame_data_provider.set_decoder(static_cast<H264MultiDecoder*>(video->video_decoder()));
      frame_allocator.set_decoder(video->video_decoder());
    }
    frame_allocator.set_pump_function([&video]() {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      static_cast<H264MultiDecoder*>(video->video_decoder())->PumpOrReschedule();
    });
    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/false, 1024 * PAGE_SIZE,
                                                   /*is_secure=*/false));
    uint32_t frame_count = 0;
    std::promise<void> wait_valid;

    frame_allocator.SetEosHandler([&wait_valid] { wait_valid.set_value(); });
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      frame_allocator.SetFrameReadyNotifier(
          [&video, &frame_count, filename](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d\n", frame_count);
            EXPECT_EQ(320u, frame->coded_width);
            EXPECT_EQ(320u, frame->display_width);
            EXPECT_EQ(192u, frame->coded_height);
            EXPECT_EQ(180u, frame->display_height);
            (void)filename;
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame.get(), filename);
#endif
            video->AssertVideoDecoderLockHeld();
            video->video_decoder()->ReturnFrame(frame);
          });

      // Initialize must happen after InitializeStreamBuffer or else it may misparse the SPS.
      EXPECT_EQ(ZX_OK, video->video_decoder()->Initialize());
    }

    auto input_h264 = TestSupport::LoadFirmwareFile(input_filename);
    ASSERT_NE(nullptr, input_h264);
    video->core()->InitializeDirectInput();
    auto nal_units = SplitNalUnits(input_h264->ptr, input_h264->size);
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      frame_data_provider.AppendFrameData(std::move(nal_units));
      auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      if (early_eos) {
        multi_decoder->QueueInputEos();
      }
      multi_decoder->ReceivedNewInput();
    }
    auto future = wait_valid.get_future();
    if (!early_eos) {
      EXPECT_EQ(std::future_status::timeout, future.wait_for(std::chrono::seconds(2)));
      EXPECT_EQ(28u, frame_count);

      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      auto* multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      multi_decoder->QueueInputEos();
    }
    EXPECT_EQ(std::future_status::ready, future.wait_for(std::chrono::seconds(10)));

    EXPECT_EQ(30u, frame_count);

    video->ClearDecoderInstance();
    video.reset();
  }

  static void DecodeMalformed(VideoInfo data,
                              const std::vector<std::pair<uint32_t, uint8_t>>& modifications) {
    SetMaxLogging();
    FakeOwner owner;
    auto video = std::make_unique<AmlogicVideo>(&owner);
    ASSERT_TRUE(video);
    TestFrameAllocator frame_allocator(video.get());

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());
    H264TestFrameDataProvider frame_data_provider(video.get());
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      video->SetDefaultInstance(
          std::make_unique<H264MultiDecoder>(video.get(), &frame_allocator, &frame_data_provider,
                                             std::nullopt, /*is_secure=*/false),
          /*hevc=*/false);
      frame_data_provider.set_decoder(static_cast<H264MultiDecoder*>(video->video_decoder()));
      frame_allocator.set_decoder(video->video_decoder());
    }
    frame_allocator.set_pump_function([&video]() {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      static_cast<H264MultiDecoder*>(video->video_decoder())->PumpOrReschedule();
    });
    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/false, 1024 * PAGE_SIZE,
                                                   /*is_secure=*/false));
    uint32_t frame_count = 0;
    std::promise<void> wait_valid;

    frame_data_provider.set_async_reset_handler([&wait_valid] { wait_valid.set_value(); });
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      frame_allocator.SetFrameReadyNotifier(
          [&video, &frame_count](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d\n", frame_count);
            video->AssertVideoDecoderLockHeld();
            video->video_decoder()->ReturnFrame(frame);
          });

      // Initialize must happen after InitializeStreamBuffer or else it may misparse the SPS.
      EXPECT_EQ(ZX_OK, video->video_decoder()->Initialize());
    }

    auto input_h264 = TestSupport::LoadFirmwareFile(data.input_filename);
    ASSERT_NE(nullptr, input_h264);
    video->core()->InitializeDirectInput();
    std::vector<uint8_t> modified_data(input_h264->ptr, input_h264->ptr + input_h264->size);
    for (auto& modification : modifications) {
      modified_data[modification.first] = modification.second;
    }
    auto nal_units = SplitNalUnits(modified_data.data(), modified_data.size());
    {
      std::lock_guard<std::mutex> lock(*video->video_decoder_lock());
      frame_data_provider.AppendFrameData(std::move(nal_units));
      auto multi_decoder = static_cast<H264MultiDecoder*>(video->video_decoder());
      multi_decoder->ReceivedNewInput();
    }
    auto future = wait_valid.get_future();
    EXPECT_EQ(std::future_status::ready, future.wait_for(std::chrono::seconds(10)));

    EXPECT_EQ(0u, frame_count);

    video->ClearDecoderInstance();
    video.reset();
  }
};

static TestH264Multi::VideoInfo bear_data = {
    .input_filename = "video_test_data/bear.h264",
    .input_hashes = bear_h264_hashes,
    .filename = "/tmp/bearmultih264.yuv",
    .coded_width = 320,
    .coded_height = 192,
    .display_width = 320,
    .display_height = 180,
    .expected_frame_count = 28,
    .has_sar = false,
};
TEST(H264Multi, DecodeBear) {
  TestH264Multi::DecodeSetStream(bear_data,
                                 /*use_parser=*/false);
}

TEST(H264Multi, DecodeBearParser) {
  TestH264Multi::DecodeSetStream(bear_data,
                                 /*use_parser=*/true);
}

TEST(H264Multi, Decode25fps) {
  TestH264Multi::VideoInfo data = {.input_filename = "video_test_data/test-25fps.h264",
                                   .input_hashes = test_25fps_h264_hashes,
                                   .filename = "/tmp/test25fpsmultih264.yuv",
                                   .coded_width = 320,
                                   .coded_height = 240,
                                   .display_width = 320,
                                   .display_height = 240,
                                   .expected_frame_count = 240,
                                   .has_sar = false};
  TestH264Multi::DecodeSetStream(data,
                                 /*use_parser=*/false);
}

TEST(H264Multi, DecodeWithSar) {
  TestH264Multi::VideoInfo data = {.input_filename = "video_test_data/red-green.h264",
                                   .input_hashes = nullptr,
                                   .filename = "/tmp/red-greenmultih264.yuv",
                                   .coded_width = 80,
                                   .coded_height = 128,
                                   .display_width = 80,
                                   .display_height = 128,
                                   .expected_frame_count = 28,
                                   .has_sar = true};
  TestH264Multi::DecodeSetStream(data,
                                 /*use_parser=*/false);
}
TEST(H264Multi, DecodeBearUnsplit) {
  TestH264Multi::DecodeUnsplit("video_test_data/bear.h264", bear_h264_hashes,
                               "/tmp/bearmultih264.yuv");
}

TEST(H264Multi, InitializeTwice) { TestH264Multi::TestInitializeTwice(); }

TEST(H264Multi, DecodeMultiInstance) { TestH264Multi::DecodeMultiInstance(); }

TEST(H264Multi, DecodeChangeConfig) { TestH264Multi::DecodeChangeConfig(); }

TEST(H264Multi, DecodeWithEarlyEos) {
  TestH264Multi::DecodeWithEos("video_test_data/bear.h264", bear_h264_hashes,
                               "/tmp/bearmultih264.yuv", /*early_eos=*/true);
}

TEST(H264Multi, DecodeWithLateEos) {
  TestH264Multi::DecodeWithEos("video_test_data/bear.h264", bear_h264_hashes,
                               "/tmp/bearmultih264.yuv", /*early_eos=*/false);
}

TEST(H264Multi, DecodeMalformedSize) {
  // This changes the height to 53184, which is too high for the hardware.
  TestH264Multi::DecodeMalformed(bear_data, {{593, 64}});
}

}  // namespace test
}  // namespace amlogic_decoder
