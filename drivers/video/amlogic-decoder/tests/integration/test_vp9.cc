// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "amlogic-video.h"

#include <byteswap.h>

#include "gtest/gtest.h"
#include "hevcdec.h"
#include "lib/fxl/logging.h"
#include "tests/test_support.h"
#include "vp9_decoder.h"

#include "bear-vp9.ivf.h"
#include "macros.h"

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

std::vector<uint32_t> TryParseSuperframeHeader(const uint8_t* data,
                                               uint32_t frame_size) {
  std::vector<uint32_t> frame_sizes;
  if (frame_size < 1)
    return frame_sizes;
  uint8_t superframe_header = data[frame_size - 1];

  // Split out superframes into separate frames - see
  // https://storage.googleapis.com/downloads.webmproject.org/docs/vp9/vp9-bitstream-specification-v0.6-20160331-draft.pdf
  // Annex B.
  if ((superframe_header & 0xc0) != 0xc0)
    return frame_sizes;
  uint8_t bytes_per_framesize = ((superframe_header >> 3) & 3) + 1;
  uint8_t superframe_count = (superframe_header & 7) + 1;
  uint32_t superframe_index_size = 2 + bytes_per_framesize * superframe_count;
  if (superframe_index_size > frame_size)
    return frame_sizes;
  if (data[frame_size - superframe_index_size] != superframe_header) {
    return frame_sizes;
  }
  const uint8_t* index_data = &data[frame_size - superframe_index_size + 1];
  uint32_t total_size = 0;
  for (uint32_t i = 0; i < superframe_count; i++) {
    uint32_t sub_frame_size;
    switch (bytes_per_framesize) {
      case 1:
        sub_frame_size = index_data[i];
        break;
      case 2:
        sub_frame_size = reinterpret_cast<const uint16_t*>(index_data)[i];
        break;
      case 4:
        sub_frame_size = reinterpret_cast<const uint32_t*>(index_data)[i];
        break;
      default:
        DECODE_ERROR("Unsupported bytes_per_framesize: %d\n",
                     bytes_per_framesize);
        frame_sizes.clear();
        return frame_sizes;
    }
    total_size += sub_frame_size;
    if (total_size > frame_size) {
      DECODE_ERROR("Total superframe size too large: %u > %u\n", total_size,
                   frame_size);
      frame_sizes.clear();
      return frame_sizes;
    }
    frame_sizes.push_back(sub_frame_size);
  }
  return frame_sizes;
}

void SplitSuperframe(const uint8_t* data, uint32_t frame_size,
                     std::vector<uint8_t>* output_vector) {
  std::vector<uint32_t> frame_sizes =
      TryParseSuperframeHeader(data, frame_size);

  if (frame_sizes.empty())
    frame_sizes.push_back(frame_size);
  uint32_t frame_offset = 0;
  uint32_t total_frame_bytes = 0;
  for (auto& size : frame_sizes) {
    total_frame_bytes += size;
  }
  const uint32_t kOutputHeaderSize = 16;
  uint32_t output_offset = output_vector->size();
  // This can be called multiple times on the same output_vector overall, but
  // should be amortized O(1), since resizing larger inserts elements at the end
  // and inserting elements at the end is amortized O(1) for std::vector.  Also,
  // for now this is for testing.
  output_vector->resize(output_offset + total_frame_bytes +
                        kOutputHeaderSize * frame_sizes.size());
  uint8_t* output = &(*output_vector)[output_offset];
  for (auto& size : frame_sizes) {
    FXL_DCHECK(output + 16 - output_vector->data() <=
               static_cast<int64_t>(output_vector->size()));
    *reinterpret_cast<uint32_t*>(output) = bswap_32(size + 4);
    output += 4;
    *reinterpret_cast<uint32_t*>(output) = ~bswap_32(size + 4);
    output += 4;
    *output++ = 0;
    *output++ = 0;
    *output++ = 0;
    *output++ = 1;
    *output++ = 'A';
    *output++ = 'M';
    *output++ = 'L';
    *output++ = 'V';

    FXL_DCHECK(output + size - output_vector->data() <=
               static_cast<int64_t>(output_vector->size()));
    memcpy(output, &data[frame_offset], size);
    output += size;
    frame_offset += size;
  }
  FXL_DCHECK(output - output_vector->data() ==
             static_cast<int64_t>(output_vector->size()));
}

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

class TestVP9 {
 public:
  static void Decode(bool use_parser, const char* filename) {
    auto video = std::make_unique<AmlogicVideo>();
    ASSERT_TRUE(video);

    EXPECT_EQ(ZX_OK, video->InitRegisters(TestSupport::parent_device()));

    video->core_ = std::make_unique<HevcDec>(video.get());
    video->core_->PowerOn();

    EXPECT_EQ(ZX_OK,
              video->InitializeStreamBuffer(
                  use_parser, use_parser ? PAGE_SIZE : 1024 * PAGE_SIZE));

    video->InitializeInterrupts();

    if (use_parser) {
      EXPECT_EQ(ZX_OK, video->InitializeEsParser());
    }

    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_ = std::make_unique<Vp9Decoder>(video.get());
      EXPECT_EQ(ZX_OK, video->video_decoder_->Initialize());
    }

    uint32_t frame_count = 0;
    std::promise<void> wait_valid;
    bool frames_returned = false;  // Protected by video->video_decoder_lock_
    std::vector<std::shared_ptr<VideoFrame>> frames_to_return;
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      video->video_decoder_->SetFrameReadyNotifier(
          [&video, &frames_to_return, &frame_count, &wait_valid,
           &frames_returned](std::shared_ptr<VideoFrame> frame) {
            ++frame_count;
            DLOG("Got frame %d\n", frame_count);
#if DUMP_VIDEO_TO_FILE
            DumpVideoFrameToFile(frame, filename);
#endif
            if (frames_returned)
              ReturnFrame(video.get(), frame);
            else
              frames_to_return.push_back(frame);
            if (frame_count == 80)
              wait_valid.set_value();
          });
    }

    // Put on a separate thread because it needs video decoding to progress in
    // order to finish.
    auto parser = std::async([&video, use_parser]() {
      auto aml_data = ConvertIvfToAmlV(bear_vp9_ivf, bear_vp9_ivf_len);
      if (use_parser) {
        video->ParseVideo(aml_data.data(), aml_data.size());
      } else {
        video->core_->InitializeDirectInput();
        video->ProcessVideoNoParser(aml_data.data(), aml_data.size());
      }
    });

    zx_nanosleep(zx_deadline_after(ZX_SEC(1)));
    {
      std::lock_guard<std::mutex> lock(video->video_decoder_lock_);
      for (auto frame : frames_to_return) {
        video->video_decoder_->ReturnFrame(frame);
      }
      frames_returned = true;
    }

    EXPECT_EQ(std::future_status::ready,
              wait_valid.get_future().wait_for(std::chrono::seconds(2)));

    EXPECT_EQ(std::future_status::ready,
              parser.wait_for(std::chrono::seconds(1)));
    video.reset();
  }

 private:
  // This is called from the interrupt handler, which already holds the lock.
  static void ReturnFrame(AmlogicVideo* video,
                          std::shared_ptr<VideoFrame> frame)
      FXL_NO_THREAD_SAFETY_ANALYSIS {
    video->video_decoder_->ReturnFrame(frame);
  }
};

TEST(VP9, Decode) { TestVP9::Decode(true, "/tmp/bearvp9.yuv"); }

TEST(VP9, DecodeNoParser) {
  TestVP9::Decode(false, "/tmp/bearvp9noparser.yuv");
}
