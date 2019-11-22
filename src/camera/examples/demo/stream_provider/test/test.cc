// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <stream_provider.h>

#include <algorithm>
#include <iostream>
#include <random>
#include <vector>

#include <gtest/gtest.h>
#include <openssl/sha.h>
#include <src/lib/syslog/cpp/logger.h>
#include <trace-provider/provider.h>
#include <trace/event.h>

class SampledHasher {
 public:
  explicit SampledHasher(size_t image_size) : sample_indices_(kSampleBytes) {
    std::mt19937 gen(0);
    std::uniform_int_distribution<uint32_t> dist(0, image_size - 1);
    for (auto& index : sample_indices_) {
      index = dist(gen);
    }
    std::sort(sample_indices_.begin(), sample_indices_.end());
  }

  // Returns the formatted sha512 string of randomly sampled bytes of an image.
  std::string Hash(const void* data) {
    auto bytes = reinterpret_cast<const uint8_t*>(data);
    std::vector<uint8_t> sample_data(kSampleBytes);
    auto it = sample_data.begin();
    for (auto index : sample_indices_) {
      *it++ = bytes[index];
    }
    constexpr char table[] = "0123456789abcdef";
    uint8_t md[SHA512_DIGEST_LENGTH]{};
    SHA512(sample_data.data(), sample_data.size(), md);
    std::string ret(2 * sizeof(md), 0);
    for (uint32_t i = 0; i < SHA512_DIGEST_LENGTH; ++i) {
      ret[2 * i] = table[(md[i] >> 4) & 0xF];
      ret[2 * i + 1] = table[md[i] & 0xF];
    }
    return ret;
  }

 private:
  static constexpr auto kSampleBytes = 16384;
  std::vector<uint32_t> sample_indices_;
};

class StreamProviderTest : public testing::TestWithParam<StreamProvider::Source> {
 protected:
  StreamProviderTest()
      : loop_(&kAsyncLoopConfigAttachToCurrentThread), trace_provider_(loop_.dispatcher()) {}

  virtual void SetUp() override {
    auto source = GetParam();
    provider_ = StreamProvider::Create(source);
    if (provider_ == nullptr) {
      // If CameraManager has ever been launched, Component Framework v1 requires that it never be
      // destroyed. As a result, if it has ever connected to the controller, it won't disconnect
      // until reboot. This means that other providers may be in use by the manager. To address
      // this, only sources that do connect are tested for correctness.
      GTEST_SKIP() << "This source may be in use by a parent component.";
    }
    RunLoopUntilIdle();
  }

  virtual void TearDown() override {
    provider_ = nullptr;
    RunLoopUntilIdle();
  }

  void RunLoopUntilIdle() { ASSERT_EQ(loop_.RunUntilIdle(), ZX_OK); }

  async::Loop loop_;
  std::unique_ptr<StreamProvider> provider_;
  trace::TraceProviderWithFdio trace_provider_;
};

// Read and validate frames from each provider type.
TEST_P(StreamProviderTest, ValidateFrames) {
  // Pick something large enough that it's likely larger than any internal ring buffers, but small
  // enough that the test completes relatively quickly.
  constexpr auto kFramesToCheck = 42u;

  // Connect to the stream.
  fuchsia::camera2::StreamPtr stream;
  auto [status, format, buffers, should_rotate] = provider_->ConnectToStream(stream.NewRequest());
  ASSERT_EQ(status, ZX_OK);
  const auto& buffer_size = buffers.settings.buffer_settings.size_bytes;
  SampledHasher hasher(buffer_size);

  bool stream_alive = true;
  stream.set_error_handler([&](zx_status_t status) {
    FX_PLOGS(ERROR, status) << provider_->GetName() << " disconnected";
    ADD_FAILURE();
    stream_alive = false;
  });

  // Sanity check some of the returned values.
  ASSERT_GT(buffers.buffer_count, 0u);
  ASSERT_NE(format.coded_width, 0u);
  ASSERT_NE(format.coded_height, 0u);
  ASSERT_GE(format.bytes_per_row, format.coded_width);

  // Populate a set of known hashes to constant-value frame data. The provider should nver return
  // frames matching these.
  std::map<std::string, uint8_t> known_hashes;
  {
    // Try known transients 0x00 and 0xFF, as well as other likely transients near values k*2^N.
    constexpr std::array kValuesToCheck{0x00, 0xFF, 0x01, 0xFE, 0x7F, 0x80, 0x3F, 0x40, 0xBF, 0xC0};
    std::vector<uint8_t> known_frame(buffer_size);
    for (size_t i = 0; i < kValuesToCheck.size(); ++i) {
      auto value = kValuesToCheck[i];
      std::cout << "\rCalculating hash for fixed value " << static_cast<uint32_t>(value) << " ("
                << i + 1 << "/" << kValuesToCheck.size() << ")";
      std::cout.flush();
      memset(known_frame.data(), value, known_frame.size());
      known_hashes[hasher.Hash(known_frame.data())] = value;
    }
    std::cout << std::endl;
  }

  // Register a frame event handler.
  std::map<std::string, uint32_t> frame_hashes;
  std::vector<bool> buffer_owned(buffers.buffer_count, false);
  uint32_t frames_received = 0;
  stream.events().OnFrameAvailable = [&, buffers =
                                             &buffers](fuchsia::camera2::FrameAvailableInfo info) {
    ASSERT_EQ(info.frame_status, fuchsia::camera2::FrameStatus::OK);
    ASSERT_LT(info.buffer_id, buffers->buffer_count);
    TRACE_DURATION_BEGIN("camera", "FrameHeld", info.buffer_id);

    if (++frames_received > kFramesToCheck) {
      stream->ReleaseFrame(info.buffer_id);
      TRACE_DURATION_END("camera", "FrameHeld", info.buffer_id);
      return;
    }

    // Check ownership validity of the buffer.
    ASSERT_LT(info.buffer_id, buffers->buffer_count);
    EXPECT_FALSE(buffer_owned[info.buffer_id])
        << "Server sent frame " << info.buffer_id << " again without the client releasing it.";
    buffer_owned[info.buffer_id] = true;

    // Map and hash the entire contents of the buffer.
    uintptr_t mapped_addr = 0;
    ASSERT_EQ(zx::vmar::root_self()->map(0, buffers->buffers[info.buffer_id].vmo, 0, buffer_size,
                                         ZX_VM_PERM_READ, &mapped_addr),
              ZX_OK);
    std::cout << "\rCalculating hash for frame " << frames_received << "/" << kFramesToCheck;
    std::cout.flush();
    auto hash = hasher.Hash(reinterpret_cast<void*>(mapped_addr));
    ASSERT_EQ(zx::vmar::root_self()->unmap(mapped_addr, buffer_size), ZX_OK);

    // Verify the hash does not match a prior or known hash. Even with a static scene, thermal
    // noise should prevent any perfectly identical frames. As a result, this check should only
    // fail if the frames are not actually coming from the sensor, or are being recycled
    // incorrectly.
    auto it = known_hashes.find(hash);
    if (it != known_hashes.end()) {
      // Frame hash matches a known constant-value hash, indicating the buffer was not correctly
      // populated.
      ADD_FAILURE_AT(__FILE__, __LINE__)
          << "Frame " << frames_received
          << " does not contain valid image data - it is just the constant byte value "
          << static_cast<uint32_t>(it->second);
    } else {
      auto it = frame_hashes.find(hash);
      if (it == frame_hashes.end()) {
        frame_hashes.emplace(hash, frames_received);
      } else {
        // Frame hash matches a prior frame's hash, indicating buffers are being recycled
        // incorrectly.
        ADD_FAILURE_AT(__FILE__, __LINE__)
            << "Duplicate frame - the contents of frames " << it->second << " and "
            << frames_received << " both hash to 0x" << hash;
      }
    }

    buffer_owned[info.buffer_id] = false;
    stream->ReleaseFrame(info.buffer_id);
    TRACE_DURATION_END("camera", "FrameHeld", info.buffer_id);
  };

  stream->Start();
  while (stream_alive && frames_received < kFramesToCheck) {
    RunLoopUntilIdle();
  }
  std::cout << std::endl;
  ASSERT_TRUE(stream_alive);
  stream->Stop();
  RunLoopUntilIdle();
}

static std::string ParamToString(testing::TestParamInfo<StreamProvider::Source> param) {
  switch (param.param) {
    case StreamProvider::Source::ISP:
      return "ISP";
    case StreamProvider::Source::CONTROLLER:
      return "CONTROLLER";
    case StreamProvider::Source::MANAGER:
      return "MANAGER";
    default:
      return "UNKNOWN";
  }
}

INSTANTIATE_TEST_SUITE_P(StreamProviderTestSuite, StreamProviderTest,
                         testing::Values(StreamProvider::Source::ISP,
                                         StreamProvider::Source::CONTROLLER,
                                         StreamProvider::Source::MANAGER),
                         ParamToString);
