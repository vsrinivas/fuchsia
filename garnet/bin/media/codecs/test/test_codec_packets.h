// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_TEST_TEST_CODEC_PACKETS_H_
#define GARNET_BIN_MEDIA_CODECS_TEST_TEST_CODEC_PACKETS_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <src/lib/fxl/logging.h>

#include <algorithm>
#include <map>

#include "gtest/gtest.h"

constexpr uint32_t kBufferLifetimeOrdinal = 1;

class CodecPacketForTest : public CodecPacket {
 public:
  CodecPacketForTest(uint32_t index)
      : CodecPacket(kBufferLifetimeOrdinal, index) {}
};

fuchsia::media::StreamBuffer StreamBufferOfSize(size_t size, uint32_t index) {
  zx::vmo vmo_handle;
  fzl::VmoMapper mapper;
  zx_status_t err = mapper.CreateAndMap(
      size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo_handle);
  FXL_CHECK(err == ZX_OK) << "Failed to create and map vmo: " << err;

  fuchsia::media::StreamBufferDataVmo vmo;
  vmo.set_vmo_handle(std::move(vmo_handle));
  vmo.set_vmo_usable_start(0);
  vmo.set_vmo_usable_size(size);

  fuchsia::media::StreamBufferData data;
  data.set_vmo(std::move(vmo));

  fuchsia::media::StreamBuffer buffer;
  buffer.set_data(std::move(data));
  buffer.set_buffer_index(index);
  buffer.set_buffer_lifetime_ordinal(kBufferLifetimeOrdinal);

  return buffer;
}

class CodecBufferForTest : public CodecBuffer {
 public:
  CodecBufferForTest(size_t size, uint32_t index)
      : CodecBuffer(/*parent=*/nullptr, kOutputPort,
                    StreamBufferOfSize(size, index)) {
    Init();
  }
};

struct TestPackets {
  std::vector<std::unique_ptr<CodecPacketForTest>> packets;
  CodecPacket* ptr(size_t i) { return packets[i].get(); }
};

TestPackets Packets(size_t count) {
  TestPackets packets;
  for (size_t i = 0; i < count; ++i) {
    packets.packets.push_back(std::make_unique<CodecPacketForTest>(i));
  }
  return packets;
}

struct TestBuffers {
  std::vector<std::unique_ptr<CodecBufferForTest>> buffers;
  const CodecBuffer* ptr(size_t i) { return buffers[i].get(); }
};

TestBuffers Buffers(std::vector<size_t> sizes) {
  TestBuffers buffers;
  for (size_t i = 0; i < sizes.size(); ++i) {
    buffers.buffers.push_back(
        std::make_unique<CodecBufferForTest>(sizes[i], i));
  }
  return buffers;
}

#endif  // GARNET_BIN_MEDIA_CODECS_TEST_TEST_CODEC_PACKETS_H_
