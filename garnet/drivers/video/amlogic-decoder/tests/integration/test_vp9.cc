// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <byteswap.h>
#include <zircon/compiler.h>

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "hevcdec.h"
#include "macros.h"
#include "pts_manager.h"
#include "tests/test_support.h"
#include "vp9_decoder.h"
#include "vp9_utils.h"

struct __attribute__((__packed__)) IvfHeader {
  uint32_t signature;
  uint16_t version;
  uint16_t header_length;
  uint32_t fourcc;
  uint16_t width;
  uint16_t height;
  uint32_t frame_rate;
  uint32_t time_scale;
  uint32_t frame_count;
  uint32_t unused;
};

struct __attribute__((__packed__)) IvfFrameHeader {
  uint32_t size_bytes;
  uint64_t presentation_timestamp;
};

struct FrameData {
  uint64_t presentation_timestamp;
  std::vector<uint8_t> data;
};

std::vector<uint8_t> ConvertIvfToAmlV(const uint8_t* data, uint32_t length) {
  uint32_t offset = sizeof(IvfHeader);
  std::vector<uint8_t> output_vector;
  while (offset < length) {
    auto frame_header = reinterpret_cast<const IvfFrameHeader*>(data + offset);
    uint32_t frame_size = frame_header->size_bytes;
    uint32_t data_offset = offset + sizeof(IvfFrameHeader);
    if (data_offset + frame_size > length) {
      DECODE_ERROR("Invalid IVF file, truncating\n");
      return output_vector;
    }

    SplitSuperframe(data + data_offset, frame_size, &output_vector);

    offset = data_offset + frame_size;
  }
  return output_vector;
}

// Split IVF-level frames
std::vector<FrameData> ConvertIvfToAmlVFrames(const uint8_t* data, uint32_t length) {
  uint32_t offset = sizeof(IvfHeader);
  std::vector<FrameData> output_vector;
  while (offset < length) {
    auto frame_header = reinterpret_cast<const IvfFrameHeader*>(data + offset);
    uint32_t frame_size = frame_header->size_bytes;
    uint32_t data_offset = offset + sizeof(IvfFrameHeader);
    if (data_offset + frame_size > length) {
      DECODE_ERROR("Invalid IVF file, truncating\n");
      return output_vector;
    }

    FrameData frame_data;
    frame_data.presentation_timestamp = frame_header->presentation_timestamp;
    SplitSuperframe(data + data_offset, frame_size, &frame_data.data);
    output_vector.push_back(std::move(frame_data));

    offset = data_offset + frame_size;
  }
  return output_vector;
}

class TestFrameProvider : public Vp9Decoder::FrameDataProvider {
 public:
  TestFrameProvider(AmlogicVideo* video, bool multi_instance)
      : video_(video), multi_instance_(multi_instance) {}

  // Always claim that 50 more bytes are available. Due to the 16kB of padding
  // at the end this is always true.
  void ReadMoreInputData(Vp9Decoder* decoder) override { decoder->UpdateDecodeSize(50); }
  void ReadMoreInputDataFromReschedule(Vp9Decoder* decoder) override { ReadMoreInputData(decoder); }

  // Called while the decoder lock is held.
  void FrameWasOutput() override __TA_NO_THREAD_SAFETY_ANALYSIS {
    DLOG("Resetting hardware\n");
    video_->SwapOutCurrentInstance();
    bool swapped_in_other = false;
    if (multi_instance_) {
      DecoderInstance* other_instance = video_->swapped_out_instances_.front().get();
      // Only try to execute from the other instance if it hasn't decoded all
      // its data yet.
      if (!other_instance->input_context() || (other_instance->input_context()->processed_video <
                                               other_instance->stream_buffer()->data_size())) {
        video_->current_instance_ = std::move(video_->swapped_out_instances_.front());
        video_->swapped_out_instances_.pop_front();
        swapped_in_other = true;
      }
    }
    if (!swapped_in_other) {
      // Swap back in the previous instance.
      video_->current_instance_ = std::move(video_->swapped_out_instances_.back());
      video_->swapped_out_instances_.pop_back();
    }
    video_->SwapInCurrentInstance();
  }

 private:
  AmlogicVideo* video_;
  bool multi_instance_;
};

class TestVP9 {
 public:
  static void Decode(bool use_parser, bool use_compressed_output, const char* input_filename,
                     const char* filename) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(
          std::make_unique<Vp9Decoder>(video.get(), Vp9Decoder::InputType::kSingleStream,
                                       use_compressed_output),
          true);
    }
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(use_parser, PAGE_SIZE, /*is_secure=*/false));

    video->InitializeInterrupts();

    if (use_parser) {
      EXPECT_EQ(ZX_OK, video->InitializeEsParser());
    }

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    bool frames_returned = false;  // Protected by video->video_decoder_lock_
    std::vector<std::weak_ptr<VideoFrame>> frames_to_return;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier([&video, &frames_to_return, &frame_count,
                                                    &wait_valid, &frames_returned,
                                                    filename](std::shared_ptr<VideoFrame> frame) {
        ++frame_count;
        DLOG("Got frame %d\n", frame_count);
        EXPECT_EQ(320u, frame->display_width);
        EXPECT_EQ(240u, frame->display_height);
        (void)filename;
#if DUMP_VIDEO_TO_FILE
        DumpVideoFrameToFile(frame.get(), filename);
#endif
        if (frames_returned)
          ReturnFrame(video.get(), frame);
        else
          frames_to_return.push_back(frame);
        if (frame_count == 241)
          wait_valid.set_value();

        if (frame_count % 5 == 0)
          SetReallocateBuffersNextFrameForTesting(video.get());
      });
    }
    auto test_ivf = TestSupport::LoadFirmwareFile(input_filename);
    ASSERT_NE(nullptr, test_ivf);

    std::atomic<bool> stop_parsing(false);
    // Put on a separate thread because it needs video decoding to progress in
    // order to finish.
    auto parser = std::async([&video, use_parser, &test_ivf, &stop_parsing]() {
      auto aml_data = ConvertIvfToAmlV(test_ivf->ptr, test_ivf->size);
      if (use_parser) {
        EXPECT_EQ(ZX_OK, video->parser()->ParseVideo(aml_data.data(), aml_data.size()));
        EXPECT_EQ(ZX_OK, video->parser()->WaitForParsingCompleted(ZX_SEC(10)));
      } else {
        video->core_->InitializeDirectInput();
        uint32_t current_offset = 0;
        uint8_t* data = aml_data.data();
        while (!stop_parsing) {
          uint32_t processed_data;
          EXPECT_EQ(ZX_OK,
                    video->ProcessVideoNoParser(data + current_offset,
                                                aml_data.size() - current_offset, &processed_data));
          current_offset += processed_data;
          if (current_offset == aml_data.size())
            break;
          zx_nanosleep(zx_deadline_after(ZX_MSEC(15)));
        }
      }
    });

    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      for (auto& frame : frames_to_return) {
        std::shared_ptr<VideoFrame> locked_ptr(frame.lock());
        if (locked_ptr) {
          video->video_decoder_->ReturnFrame(std::move(locked_ptr));
        }
      }
      frames_returned = true;
    }

    EXPECT_EQ(std::future_status::ready, wait_valid.get_future().wait_for(std::chrono::seconds(2)));

    stop_parsing = true;

    EXPECT_EQ(std::future_status::ready, parser.wait_for(std::chrono::seconds(1)));
    video.reset();
  }

  static void DecodePerFrame() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    auto test_ivf = TestSupport::LoadFirmwareFile("video_test_data/test-25fps.vp9");
    ASSERT_NE(nullptr, test_ivf);
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(
          std::make_unique<Vp9Decoder>(video.get(), Vp9Decoder::InputType::kSingleStream, false),
          true);
    }

    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/true, PAGE_SIZE,
                                                   /*is_secure=*/false));

    video->InitializeInterrupts();

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    std::vector<std::shared_ptr<VideoFrame>> frames_to_return;
    uint64_t next_pts = 0;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count, &wait_valid, &next_pts](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d, pts: %ld\n", frame_count, frame->pts);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame, filename);
#endif
            EXPECT_TRUE(frame->has_pts);
            // All frames are shown, so pts should be in order. Due to rounding,
            // pts may be 1 off.
            EXPECT_LE(next_pts, frame->pts);
            EXPECT_GE(next_pts + 1, frame->pts);

            // 25 fps video
            next_pts = frame->pts + 1000 / 25;
            ReturnFrame(video.get(), frame);
            if (frame_count == 241)
              wait_valid.set_value();
          });
    }

    // Put on a separate thread because it needs video decoding to progress in
    // order to finish.
    auto parser = std::async([&video, &test_ivf]() {
      auto aml_data = ConvertIvfToAmlVFrames(test_ivf->ptr, test_ivf->size);
      uint32_t stream_offset = 0;
      for (auto& data : aml_data) {
        video->pts_manager()->InsertPts(stream_offset, true, data.presentation_timestamp);
        EXPECT_EQ(ZX_OK, video->parser()->ParseVideo(data.data.data(), data.data.size()));
        EXPECT_EQ(ZX_OK, video->parser()->WaitForParsingCompleted(ZX_SEC(10)));
        stream_offset += data.data.size();
      }
    });

    EXPECT_EQ(std::future_status::ready, wait_valid.get_future().wait_for(std::chrono::seconds(2)));

    EXPECT_EQ(std::future_status::ready, parser.wait_for(std::chrono::seconds(1)));
    video.reset();
  }

  static void DecodeResetHardware(const char* filename) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(
          std::make_unique<Vp9Decoder>(video.get(), Vp9Decoder::InputType::kMultiStream, false),
          true);
    }
    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/false, 1024 * PAGE_SIZE,
                                                   /*is_secure=*/false));

    video->InitializeInterrupts();

    TestFrameProvider frame_provider(video.get(), false);
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      static_cast<Vp9Decoder*>(video->video_decoder_)->SetFrameDataProvider(&frame_provider);
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count, &wait_valid](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d\n", frame_count);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame.get(), filename);
#endif
            ReturnFrame(video.get(), frame);
            // Only 49 of the first 50 frames are shown.
            if (frame_count == 49)
              wait_valid.set_value();
          });
    }

    auto test_ivf = TestSupport::LoadFirmwareFile("video_test_data/test-25fps.vp9");
    ASSERT_NE(nullptr, test_ivf);
    auto aml_data = ConvertIvfToAmlVFrames(test_ivf->ptr, test_ivf->size);
    video->core_->InitializeDirectInput();
    // Only use the first 50 frames to save time.
    for (uint32_t i = 0; i < 50; i++) {
      EXPECT_EQ(ZX_OK,
                video->ProcessVideoNoParser(aml_data[i].data.data(), aml_data[i].data.size()));
    }
    // Force all frames to be processed.
    uint8_t padding[16384] = {};
    EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(padding, sizeof(padding)));
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      static_cast<Vp9Decoder*>(video->video_decoder())->UpdateDecodeSize(50);
    }

    EXPECT_EQ(std::future_status::ready, wait_valid.get_future().wait_for(std::chrono::seconds(2)));

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->current_instance_.reset();
      video->video_decoder_ = nullptr;
    }
    video.reset();
  }

  static void DecodeMultiInstance() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    TestFrameProvider frame_provider(video.get(), true);

    for (uint32_t i = 0; i < 2; i++) {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      auto decoder =
          std::make_unique<Vp9Decoder>(video.get(), Vp9Decoder::InputType::kMultiStream, false);
      decoder->SetFrameDataProvider(&frame_provider);
      EXPECT_EQ(ZX_OK, decoder->InitializeBuffers());
      video->swapped_out_instances_.push_back(
          std::make_unique<DecoderInstance>(std::move(decoder), video->hevc_core_.get()));
      StreamBuffer* buffer = video->swapped_out_instances_.back()->stream_buffer();
      EXPECT_EQ(ZX_OK, video->AllocateStreamBuffer(buffer, PAGE_SIZE * 1024, /*use_parser=*/false,
                                                   /*is_secure=*/false));
    }

    {
      // TODO: Use production code to schedule in the first instance.
      // AmlogicVideo::TryToSchedule() currently tries to read data and start
      // decoding, which is not quite what we want here.
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->current_instance_ = std::move(video->swapped_out_instances_.front());
      video->swapped_out_instances_.pop_front();
      video->video_decoder_ = video->current_instance_->decoder();
      video->stream_buffer_ = video->current_instance_->stream_buffer();
      video->core_ = video->current_instance_->core();
      video->core_->PowerOn();
      EXPECT_EQ(ZX_OK, static_cast<Vp9Decoder*>(video->video_decoder_)->InitializeHardware());
    }

    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    video->InitializeStreamInput(false);

    video->InitializeInterrupts();

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count, &wait_valid](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d\n", frame_count);
            DLOG("coded_width: %d, coded_height: %d\n", frame->coded_width, frame->coded_height);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame.get(), "/tmp/bearmulti1.yuv");
#endif
            ReturnFrame(video.get(), frame);
            // Only 49 of the first 50 frames are shown.
            if (frame_count == 49)
              wait_valid.set_value();
          });
    }
    uint32_t frame_count1 = 0;
    std::promise<void> wait_valid1;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->swapped_out_instances_.back()->decoder()->SetFrameReadyNotifier(
          [&video, &frame_count1, &wait_valid1](std::shared_ptr<VideoFrame> frame) {
            ++frame_count1;
            DLOG("Decoder 2 Got frame %d\n", frame_count1);
            EXPECT_EQ(320u, frame->display_width);
            EXPECT_EQ(240u, frame->display_height);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame.get(), "/tmp/bearmulti2.yuv");
#endif
            ReturnFrame(video.get(), frame);
            if (frame_count1 == 30)
              wait_valid1.set_value();
          });
    }

    // The default stack size is ZIRCON_DEFAULT_STACK_SIZE - 256kB.
    uint8_t padding[16384] = {};
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      StreamBuffer* buffer = video->current_instance_->stream_buffer();
      auto test_ivf = TestSupport::LoadFirmwareFile("video_test_data/test-25fps.vp9");
      ASSERT_NE(nullptr, test_ivf);
      auto aml_data = ConvertIvfToAmlVFrames(test_ivf->ptr, test_ivf->size);
      video->core_->InitializeDirectInput();
      // Only use the first 50 frames to save time.
      for (uint32_t i = 0; i < 50; i++) {
        EXPECT_EQ(ZX_OK,
                  video->ProcessVideoNoParser(aml_data[i].data.data(), aml_data[i].data.size()));
      }
      buffer->set_padding_size(sizeof(padding));
      // Force all frames to be processed.
      EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(padding, sizeof(padding)));
    }

    // Normally we'd probably want to always fill the stream buffer when the
    // decoder is attached to the hardware, but for testing we should try
    // filling the buffer when it's not attached, to ensure we can correctly
    // initialize the write pointer later.
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      auto test_ivf2 = TestSupport::LoadFirmwareFile("video_test_data/test-25fps.vp9_2");
      ASSERT_NE(nullptr, test_ivf2);
      auto aml_data2 = ConvertIvfToAmlVFrames(test_ivf2->ptr, test_ivf2->size);
      StreamBuffer* buffer = video->swapped_out_instances_.back()->stream_buffer();
      uint32_t offset = 0;
      // Only use the first 30 frames to save time. Ensure this is different
      // from above, to test whether ending decoding early works.
      for (uint32_t i = 0; i < 30; i++) {
        memcpy(buffer->buffer().virt_base() + offset, aml_data2[i].data.data(),
               aml_data2[i].data.size());
        offset += aml_data2[i].data.size();
      }
      buffer->set_data_size(offset);
      buffer->set_padding_size(sizeof(padding));
      memcpy(buffer->buffer().virt_base() + offset, padding, sizeof(padding));
      offset += sizeof(padding);
      buffer->buffer().CacheFlush(0, offset);
    }
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      static_cast<Vp9Decoder*>(video->video_decoder())->UpdateDecodeSize(50);
    }

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(10)));

    EXPECT_EQ(std::future_status::ready,
              wait_valid1.get_future().wait_for(std::chrono::seconds(10)));

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->current_instance_.reset();
      video->swapped_out_instances_.clear();
      video->video_decoder_ = nullptr;
    }
    video.reset();
  }

 private:
  // This is called from the interrupt handler, which already holds the lock.
  static void ReturnFrame(AmlogicVideo* video,
                          std::shared_ptr<VideoFrame> frame) __TA_NO_THREAD_SAFETY_ANALYSIS {
    video->video_decoder_->ReturnFrame(frame);
  }

  static void SetReallocateBuffersNextFrameForTesting(AmlogicVideo* video)
      __TA_NO_THREAD_SAFETY_ANALYSIS {
    static_cast<Vp9Decoder*>(video->video_decoder_)
        ->set_reallocate_buffers_next_frame_for_testing();
  }
};

class VP9Compression : public ::testing::TestWithParam</*compressed_output=*/bool> {};

TEST_P(VP9Compression, Decode) {
  TestVP9::Decode(true, GetParam(), "video_test_data/test-25fps.vp9", "/tmp/bearvp9.yuv");
}

TEST_P(VP9Compression, DecodeNoParser) {
  TestVP9::Decode(false, GetParam(), "video_test_data/test-25fps.vp9", "/tmp/bearvp9noparser.yuv");
}

TEST_P(VP9Compression, Decode10Bit) {
  TestVP9::Decode(false, GetParam(), "video_test_data/test-25fps.vp9_2",
                  "/tmp/bearvp9noparser.yuv");
}

INSTANTIATE_TEST_SUITE_P(VP9CompressionOptional, VP9Compression, ::testing::Bool());

TEST(VP9, DecodePerFrame) { TestVP9::DecodePerFrame(); }

TEST(VP9, DecodeResetHardware) { TestVP9::DecodeResetHardware("/tmp/bearvp9reset.yuv"); }

TEST(VP9, DecodeMultiInstance) { TestVP9::DecodeMultiInstance(); }
