// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_CODEC_CODECS_TEST_TEST_CODEC_PACKETS_H_
#define SRC_MEDIA_CODEC_CODECS_TEST_TEST_CODEC_PACKETS_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <map>

#include "src/media/lib/codec_impl/include/lib/media/codec_impl/codec_buffer.h"
#include "src/media/lib/codec_impl/include/lib/media/codec_impl/codec_packet.h"

constexpr uint32_t kBufferLifetimeOrdinal = 1;

class CodecPacketForTest : public CodecPacket {
 public:
  CodecPacketForTest(uint32_t index) : CodecPacket(kBufferLifetimeOrdinal, index) {}
};

static CodecVmoRange VmoRangeOfSize(size_t size) {
  zx::vmo vmo_handle;
  fzl::VmoMapper mapper;
  zx_status_t err =
      mapper.CreateAndMap(size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, nullptr, &vmo_handle);
  FX_CHECK(err == ZX_OK) << "Failed to create and map vmo: " << err;

  return CodecVmoRange(std::move(vmo_handle), 0, size);
}

class CodecBufferForTest : public CodecBuffer {
 public:
  CodecBufferForTest(size_t size, uint32_t index, bool is_secure)
      : CodecBuffer(/*parent=*/nullptr,
                    Info{.port = kOutputPort,
                         .lifetime_ordinal = kBufferLifetimeOrdinal,
                         .index = index,
                         .is_secure = is_secure},
                    VmoRangeOfSize(size)) {
    if (!Map()) {
      ZX_PANIC("CodecBufferForTest() failed to Map()");
    }
  }
};

struct TestPackets {
  std::vector<std::unique_ptr<CodecPacketForTest>> packets;
  CodecPacket* ptr(size_t i) { return packets[i].get(); }
};

static inline TestPackets Packets(size_t count) {
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

static inline TestBuffers Buffers(std::vector<size_t> sizes) {
  TestBuffers buffers;
  for (size_t i = 0; i < sizes.size(); ++i) {
    constexpr bool kIsSecure = false;
    buffers.buffers.push_back(std::make_unique<CodecBufferForTest>(sizes[i], i, kIsSecure));
  }
  return buffers;
}

#endif  // SRC_MEDIA_CODEC_CODECS_TEST_TEST_CODEC_PACKETS_H_
