// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxt/serializer.h>

#include <gtest/gtest.h>

namespace {
struct FakeRecord {
  void WriteWord(uint64_t word) {
    uint8_t* bytes = reinterpret_cast<uint8_t*>(&word);
    for (unsigned i = 0; i < 8; i++) {
      bytes_.push_back(bytes[i]);
    }
  }
  void WriteBytes(const void* buffer, size_t num_bytes) {
    for (unsigned i = 0; i < num_bytes; i++) {
      bytes_.push_back(reinterpret_cast<const uint8_t*>(buffer)[i]);
    }

    // 0 pad the buffer to an 8 byte boundary
    for (size_t i = num_bytes; i % 8 != 0; i++) {
      bytes_.push_back(0);
    }
  }
  void Commit() {
    // Records must only be committed once
    EXPECT_FALSE(committed);
    committed = true;
  }

  bool committed = false;
  std::vector<uint8_t>& bytes_;
};

struct FakeWriter {
  using Reservation = FakeRecord;
  std::vector<uint8_t> bytes;
  zx::status<FakeRecord> Reserve(uint64_t header) {
    FakeRecord rec{false, bytes};
    rec.WriteWord(header);
    return zx::ok(rec);
  }
};

struct FakeNoMemWriter {
  using Reservation = FakeRecord;
  zx::status<FakeRecord> Reserve(uint64_t header) { return zx::error(ZX_ERR_NO_MEMORY); }
};

TEST(Serializer, InitRecord) {
  FakeWriter writer_success;
  EXPECT_EQ(ZX_OK, fxt::WriteInitializationRecord(&writer_success, 0xABCD));
  EXPECT_EQ(writer_success.bytes.size(), 16U);

  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer_success.bytes.data());

  // We expect to see:
  // Word 0:
  // Bits [0 .. 3]: The record type (1)
  // Bits [4 .. 15]: The record type size in 64bit words (2)
  // Word 1:
  // The number of ticks per second
  EXPECT_EQ(bytes[0], uint64_t{0x0000'0000'0000'0021});
  EXPECT_EQ(bytes[1], uint64_t{0x0000'0000'0000'ABCD});

  FakeNoMemWriter writer_no_mem;
  EXPECT_EQ(ZX_ERR_NO_MEMORY, fxt::WriteInitializationRecord(&writer_no_mem, 0xABCD));
}
}  // namespace
