// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <byteswap.h>
#include <zircon/compiler.h>

#include <openssl/sha.h>

#include "amlogic-video.h"
#include "gtest/gtest.h"
#include "hevcdec.h"
#include "macros.h"
#include "pts_manager.h"
#include "test_25fps_vp9_hashes.h"
#include "test_frame_allocator.h"
#include "tests/test_support.h"
#include "video_frame_helpers.h"
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

class TestFrameProvider final : public Vp9Decoder::FrameDataProvider {
 public:
  // Always claim that 50 more bytes are available. Due to the 16kB of padding
  // at the end this is always true.
  void ReadMoreInputData(Vp9Decoder* decoder) override { decoder->UpdateDecodeSize(50); }
  void ReadMoreInputDataFromReschedule(Vp9Decoder* decoder) override { ReadMoreInputData(decoder); }
  bool HasMoreInputData() override {
    // If the input context hasn't been created yet then no data has been
    // decoded, so more must exist.
    return (!instance_->input_context() || (instance_->input_context()->processed_video <
                                            instance_->stream_buffer()->data_size()));
  }

  void set_instance(DecoderInstance* instance) { instance_ = instance; }

 private:
  DecoderInstance* instance_ = nullptr;
};

// Repeatedly try to process video, either it's all processed or until a flag is set.
static void FeedDataUntilFlag(AmlogicVideo* video, const uint8_t* input, uint32_t input_size,
                              std::atomic<bool>* stop_parsing) {
  uint32_t current_offset = 0;
  while (!*stop_parsing) {
    uint32_t processed_data;
    EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(input + current_offset,
                                                 input_size - current_offset, &processed_data));
    current_offset += processed_data;
    if (current_offset == input_size)
      break;
    zx_nanosleep(zx_deadline_after(ZX_MSEC(15)));
  }
}

static const uint8_t kFlushThroughBytes[16384] = {};
constexpr uint32_t kTestVideoFrameCount = 249;

class TestVP9 {
 public:
  static void Decode(bool use_parser, bool use_compressed_output, bool delayed_return,
                     const char* input_filename, const char* filename, bool test_hashes) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());

    TestFrameAllocator frame_allocator(video.get());

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(
          std::make_unique<Vp9Decoder>(video.get(), Vp9Decoder::InputType::kSingleStream,
                                       use_compressed_output, false),
          true);
    }
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(use_parser, PAGE_SIZE, /*is_secure=*/false));

    if (use_parser) {
      EXPECT_EQ(ZX_OK, video->InitializeEsParser());
    }

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      frame_allocator.set_decoder(video->video_decoder_);
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    bool frames_returned = false;  // Protected by video->video_decoder_lock_
    std::vector<std::weak_ptr<VideoFrame>> frames_to_return;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frames_to_return, &frame_count, &wait_valid, &frames_returned, delayed_return,
           filename, test_hashes](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d\n", frame_count);
            EXPECT_EQ(320u, frame->display_width);
            EXPECT_EQ(240u, frame->display_height);
            (void)filename;
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame.get(), filename);
#endif
            if (test_hashes) {
              uint8_t md[SHA256_DIGEST_LENGTH];
              HashFrame(frame.get(), md);
              EXPECT_EQ(0, memcmp(md, test_25fps_hashes[frame_count - 1], sizeof(md)))
                  << "Incorrect hash for frame " << frame_count << ": " << StringifyHash(md);
            }
            if (frames_returned || !delayed_return)
              ReturnFrame(video.get(), frame);
            else
              frames_to_return.push_back(frame);
            if (frame_count == kTestVideoFrameCount)
              wait_valid.set_value();

            // Testing delayed return doesn't work well with reallocating buffers, since the
            // decoder will throw out the old buffers and continue decoding anyway.
            if (!delayed_return && (frame_count % 5 == 0))
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
        EXPECT_EQ(ZX_OK,
                  video->parser()->ParseVideo(kFlushThroughBytes, sizeof(kFlushThroughBytes)));
        EXPECT_EQ(ZX_OK, video->parser()->WaitForParsingCompleted(ZX_SEC(10)));
      } else {
        video->core_->InitializeDirectInput();
        FeedDataUntilFlag(video.get(), aml_data.data(), aml_data.size(), &stop_parsing);
        FeedDataUntilFlag(video.get(), kFlushThroughBytes, sizeof(kFlushThroughBytes),
                          &stop_parsing);
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

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(10)));

    stop_parsing = true;

    EXPECT_EQ(std::future_status::ready, parser.wait_for(std::chrono::seconds(1)));
    video.reset();
  }

  static void DecodePerFrame() {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);
    TestFrameAllocator frame_allocator(video.get());

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());

    auto test_ivf = TestSupport::LoadFirmwareFile("video_test_data/test-25fps.vp9");
    ASSERT_NE(nullptr, test_ivf);
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(
          std::make_unique<Vp9Decoder>(video.get(), Vp9Decoder::InputType::kSingleStream, false,
                                       false),
          true);
    }

    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/true, PAGE_SIZE,
                                                   /*is_secure=*/false));

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      frame_allocator.set_decoder(video->video_decoder_);
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
            if (frame_count == kTestVideoFrameCount)
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
      EXPECT_EQ(ZX_OK, video->parser()->ParseVideo(kFlushThroughBytes, sizeof(kFlushThroughBytes)));
      EXPECT_EQ(ZX_OK, video->parser()->WaitForParsingCompleted(ZX_SEC(10)));
    });

    EXPECT_EQ(std::future_status::ready, wait_valid.get_future().wait_for(std::chrono::seconds(2)));

    EXPECT_EQ(std::future_status::ready, parser.wait_for(std::chrono::seconds(1)));
    video.reset();
  }

  static void DecodeResetHardware(const char* filename, bool use_parser) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);
    TestFrameAllocator frame_allocator(video.get());

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(std::make_unique<Vp9Decoder>(
                                    video.get(), Vp9Decoder::InputType::kMultiStream, false, false),
                                true);
    }
    // Don't use parser, because we need to be able to save and restore the read
    // and write pointers, which can't be done if the parser is using them as
    // well.
    EXPECT_EQ(ZX_OK, video->InitializeStreamBuffer(/*use_parser=*/false, 1024 * PAGE_SIZE,
                                                   /*is_secure=*/false));

    TestFrameProvider frame_provider;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      frame_allocator.set_decoder(video->video_decoder_);
      static_cast<Vp9Decoder*>(video->video_decoder_)->SetFrameDataProvider(&frame_provider);
      frame_provider.set_instance(video->current_instance());
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
            if (frame_count == 50)
              wait_valid.set_value();
          });
    }

    auto test_ivf = TestSupport::LoadFirmwareFile("video_test_data/test-25fps.vp9");
    ASSERT_NE(nullptr, test_ivf);
    auto aml_data = ConvertIvfToAmlVFrames(test_ivf->ptr, test_ivf->size);
    video->core_->InitializeDirectInput();
    const uint8_t kPadding[16384] = {};
    if (use_parser) {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      EXPECT_EQ(ZX_OK, video->parser()->InitializeEsParser(nullptr));
      video->parser()->SyncFromDecoderInstance(video->current_instance());
      for (uint32_t i = 0; i < 50; i++) {
        EXPECT_EQ(ZX_OK,
                  video->parser()->ParseVideo(aml_data[i].data.data(), aml_data[i].data.size()));
        EXPECT_EQ(ZX_OK, video->parser()->WaitForParsingCompleted(ZX_SEC(1)));
      }
      // Force all frames to be processed.
      EXPECT_EQ(ZX_OK, video->parser()->ParseVideo(kPadding, sizeof(kPadding)));
      EXPECT_EQ(ZX_OK, video->parser()->WaitForParsingCompleted(ZX_SEC(1)));
      video->parser()->SyncToDecoderInstance(video->current_instance());
    } else {
      // Only use the first 50 frames to save time.
      for (uint32_t i = 0; i < 50; i++) {
        EXPECT_EQ(ZX_OK,
                  video->ProcessVideoNoParser(aml_data[i].data.data(), aml_data[i].data.size()));
      }
      // Force all frames to be processed.
      EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(kPadding, sizeof(kPadding)));
    }
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

  static void DecodeMultiInstance(bool inject_initialization_fault) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());

    std::vector<std::unique_ptr<TestFrameProvider>> frame_providers;
    std::vector<std::unique_ptr<TestFrameAllocator>> frame_allocators;

    for (uint32_t i = 0; i < 2; i++) {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      auto decoder = std::make_unique<Vp9Decoder>(video.get(), Vp9Decoder::InputType::kMultiStream,
                                                  false, false);
      frame_providers.push_back(std::make_unique<TestFrameProvider>());
      decoder->SetFrameDataProvider(frame_providers.back().get());
      auto frame_allocator = std::make_unique<TestFrameAllocator>(video.get());
      frame_allocator->set_decoder(decoder.get());
      frame_allocators.push_back(std::move(frame_allocator));
      EXPECT_EQ(ZX_OK, decoder->InitializeBuffers());
      video->swapped_out_instances_.push_back(
          std::make_unique<DecoderInstance>(std::move(decoder), video->hevc_core_.get()));
      StreamBuffer* buffer = video->swapped_out_instances_.back()->stream_buffer();
      EXPECT_EQ(ZX_OK, video->AllocateStreamBuffer(buffer, PAGE_SIZE * 1024, /*use_parser=*/false,
                                                   /*is_secure=*/false));
      frame_providers.back()->set_instance(video->swapped_out_instances_.back().get());
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
            if (frame_count == 50)
              wait_valid.set_value();
          });
    }
    uint32_t frame_count1 = 0;
    std::promise<void> wait_valid1;
    bool got_error = false;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      VideoDecoder* second_decoder = video->swapped_out_instances_.back()->decoder();
      second_decoder->SetFrameReadyNotifier(
          [&video, &frame_count1, &wait_valid1,
           inject_initialization_fault](std::shared_ptr<VideoFrame> frame) {
            // This is called from the interrupt handler, which already holds the lock.
            video->AssertVideoDecoderLockHeld();
            ++frame_count1;
            DLOG("Decoder 2 Got frame %d\n", frame_count1);
            EXPECT_EQ(320u, frame->display_width);
            EXPECT_EQ(240u, frame->display_height);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame.get(), "/tmp/bearmulti2.yuv");
#endif
            ReturnFrame(video.get(), frame);
            constexpr uint32_t kFrameToFaultAt = 20;
            if (frame_count1 == kFrameToFaultAt && inject_initialization_fault) {
              static_cast<Vp9Decoder*>(video->video_decoder())->InjectInitializationFault();
            }
            if (inject_initialization_fault) {
              // If an initialization fault was injected, decoding shouldn't continue.
              EXPECT_LE(frame_count1, kFrameToFaultAt);
            } else {
              if (frame_count1 == 30)
                wait_valid1.set_value();
            }
          });
      second_decoder->SetErrorHandler([&got_error, &wait_valid1]() {
        got_error = true;
        wait_valid1.set_value();
      });
    }

    // The default stack size is ZIRCON_DEFAULT_STACK_SIZE - 256kB.
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
      buffer->set_padding_size(sizeof(kFlushThroughBytes));
      // Force all frames to be processed.
      EXPECT_EQ(ZX_OK, video->ProcessVideoNoParser(kFlushThroughBytes, sizeof(kFlushThroughBytes)));
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
      buffer->set_padding_size(sizeof(kFlushThroughBytes));
      memcpy(buffer->buffer().virt_base() + offset, kFlushThroughBytes, sizeof(kFlushThroughBytes));
      offset += sizeof(kFlushThroughBytes);
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

    EXPECT_EQ(50u, frame_count);
    if (inject_initialization_fault) {
      EXPECT_TRUE(got_error);
      EXPECT_EQ(20u, frame_count1);
    } else {
      EXPECT_FALSE(got_error);
      EXPECT_EQ(30u, frame_count1);
    }

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->current_instance_.reset();
      video->swapped_out_instances_.clear();
      video->video_decoder_ = nullptr;
    }
    video.reset();
  }

  static void DecodeMalformed(const char* input_filename, uint32_t modification_offset,
                              uint8_t modification_value) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));
    EXPECT_EQ(ZX_OK, video->InitDecoder());

    TestFrameAllocator frame_allocator(video.get());
    std::promise<void> first_wait_valid;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->SetDefaultInstance(
          std::make_unique<Vp9Decoder>(video.get(), Vp9Decoder::InputType::kSingleStream,
                                       /*use_compressed_output=*/false, false),
          true);
      video->video_decoder()->SetErrorHandler([&first_wait_valid]() {
        DLOG("Got decode error");
        first_wait_valid.set_value();
      });
    }
    EXPECT_EQ(ZX_OK,
              video->InitializeStreamBuffer(/*use_parser=*/true, PAGE_SIZE, /*is_secure=*/false));

    EXPECT_EQ(ZX_OK, video->InitializeEsParser());

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      frame_allocator.set_decoder(video->video_decoder_);
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

    uint32_t frame_count = 0;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frame_count](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DECODE_ERROR("Got frame %d", frame_count);
            DLOG("Got frame %d\n", frame_count);
            EXPECT_EQ(320u, frame->display_width);
            EXPECT_EQ(240u, frame->display_height);
            ReturnFrame(video.get(), frame);
          });
    }
    auto test_ivf = TestSupport::LoadFirmwareFile(input_filename);
    ASSERT_NE(nullptr, test_ivf);

    auto aml_data = ConvertIvfToAmlV(test_ivf->ptr, test_ivf->size);
    // Arbitrary modifications to an AMLV header shouldn't happen in production code,
    // because the driver is what creates that. The rest is fair game, though.
    aml_data[modification_offset] = modification_value;
    EXPECT_EQ(ZX_OK, video->parser()->ParseVideo(aml_data.data(), aml_data.size()));
    EXPECT_EQ(std::future_status::ready,
              first_wait_valid.get_future().wait_for(std::chrono::seconds(1)));
    // The decoder should now be hung without having gotten through all the input so we should
    // cancel parsing before teardown.
    video->parser()->CancelParsing();

    video.reset();
  }

 private:
  // This is called from the interrupt handler, which already holds the lock.
  static void ReturnFrame(AmlogicVideo* video, std::shared_ptr<VideoFrame> frame) {
    video->AssertVideoDecoderLockHeld();
    video->video_decoder_->ReturnFrame(frame);
  }

  static void SetReallocateBuffersNextFrameForTesting(AmlogicVideo* video) {
    video->AssertVideoDecoderLockHeld();
    static_cast<Vp9Decoder*>(video->video_decoder_)
        ->set_reallocate_buffers_next_frame_for_testing();
  }
};

class VP9Compression : public ::testing::TestWithParam</*compressed_output=*/bool> {};

TEST_P(VP9Compression, Decode) {
  TestVP9::Decode(true, GetParam(), false, "video_test_data/test-25fps.vp9", "/tmp/bearvp9.yuv",
                  true);
}

TEST_P(VP9Compression, DecodeDelayedReturn) {
  TestVP9::Decode(true, GetParam(), true, "video_test_data/test-25fps.vp9", "/tmp/bearvp9.yuv",
                  true);
}

TEST_P(VP9Compression, DecodeNoParser) {
  TestVP9::Decode(false, GetParam(), false, "video_test_data/test-25fps.vp9",
                  "/tmp/bearvp9noparser.yuv", true);
}

TEST_P(VP9Compression, Decode10Bit) {
  TestVP9::Decode(false, GetParam(), false, "video_test_data/test-25fps.vp9_2",
                  "/tmp/bearvp9noparser.yuv", false);
}

INSTANTIATE_TEST_SUITE_P(VP9CompressionOptional, VP9Compression, ::testing::Bool());

TEST(VP9, DecodePerFrame) { TestVP9::DecodePerFrame(); }

TEST(VP9, DecodeResetHardware) { TestVP9::DecodeResetHardware("/tmp/bearvp9reset.yuv", false); }
TEST(VP9, DecodeResetHardwareWithParser) {
  TestVP9::DecodeResetHardware("/tmp/bearvp9resetwithparser.yuv", true);
}

TEST(VP9, DecodeMultiInstance) { TestVP9::DecodeMultiInstance(false); }

TEST(VP9, DecodeMultiInstanceWithInitializationFault) { TestVP9::DecodeMultiInstance(true); }

TEST(VP9, DecodeMalformedHang) {
  // Numbers are essentially random, but picked to ensure the decoder would
  // normally hang. The offset should be >= 16 to avoid hitting the AMLV header.
  TestVP9::DecodeMalformed("video_test_data/test-25fps.vp9", 17, 10);
}
