// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxt/serializer.h>

#include <gtest/gtest.h>

#include "lib/fxt/fields.h"
#include "lib/fxt/record_types.h"

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

TEST(Serializer, IndexedStringReferences) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);

  FakeWriter writer;
  EXPECT_EQ(ZX_OK,
            fxt::WriteInstantEventRecord(&writer, event_time, thread_ref, category_ref, name_ref));
  // Everything should be a reference, so we should only see two words
  EXPECT_EQ(writer.bytes.size(), 16U);
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = bytes[0];
  // We expect to see our string references:
  // [48 .. 63]: name (string ref)
  EXPECT_EQ(header & 0xFFFF'0000'0000'0000, uint64_t{0x1234'0000'0000'0000});
  // [32 .. 47]: category (string ref)
  EXPECT_EQ(header & 0x0000'FFFF'0000'0000, uint64_t{0x0000'7777'0000'0000});
}

TEST(Serializer, InlineStringReferences) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_inline("category");
  fxt::StringRef name_inline("name longer than eight bytes");

  FakeWriter inline_writer;
  EXPECT_EQ(ZX_OK, fxt::WriteInstantEventRecord(&inline_writer, event_time, thread_ref,
                                                category_inline, name_inline));
  // Everything should be inline, so we should see two words for the header and
  // timestamp, plus 1 word for "category", plus 4 words for "name longer than
  // eight bytes".
  EXPECT_EQ(inline_writer.bytes.size(), fxt::WordSize(7).SizeInBytes());
  uint64_t* inline_bytes = reinterpret_cast<uint64_t*>(inline_writer.bytes.data());
  uint64_t inline_header = inline_bytes[0];
  // We expect our header to indicate inline stringrefs (msb of 1, lower 15 bits denote length)
  // [48 .. 63]: name (string ref)
  EXPECT_EQ(inline_header & 0xFFFF'0000'0000'0000, uint64_t{0x801c'0000'0000'0000});
  // [32 .. 47]: category (string ref)
  EXPECT_EQ(inline_header & 0x0000'FFFF'0000'0000, uint64_t{0x0000'8008'0000'0000});

  char* category_stream = reinterpret_cast<char*>(inline_bytes + 2);
  EXPECT_EQ(std::memcmp(category_stream, "category", 8), 0);
  char* name_stream1 = reinterpret_cast<char*>(inline_bytes + 3);
  EXPECT_EQ(std::memcmp(name_stream1, "name longer than eight bytes\0\0\0\0", 32), 0);
}

TEST(Serializer, IndexThreadReferences) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef index_thread_ref(0xAB);
  fxt::StringRef category(1);
  fxt::StringRef name(2);

  FakeWriter writer;
  EXPECT_EQ(ZX_OK,
            fxt::WriteInstantEventRecord(&writer, event_time, index_thread_ref, category, name));
  // Everything should be indexed, so we should see two words for the header and
  // timestamp
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(2).SizeInBytes());
  uint64_t* index_bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t index_header = index_bytes[0];

  // We expect our header to contain our threadref
  // [24 .. 31]: thread (thread ref)
  EXPECT_EQ(index_header & 0x0000'0000'FF00'0000, uint64_t{0x0000'0000'AB00'0000});
}

TEST(Serializer, InlineThreadReferences) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef inline_thread_ref(0xDEADBEEF, 0xCAFEF00D);
  fxt::StringRef category(1);
  fxt::StringRef name(2);

  FakeWriter inline_writer;
  EXPECT_EQ(ZX_OK, fxt::WriteInstantEventRecord(&inline_writer, event_time, inline_thread_ref,
                                                category, name));

  // We should see two extra words to include the 2 koids
  EXPECT_EQ(inline_writer.bytes.size(), fxt::WordSize(4).SizeInBytes());

  uint64_t* inline_bytes = reinterpret_cast<uint64_t*>(inline_writer.bytes.data());
  uint64_t inline_header = inline_bytes[0];

  // We expect our header to indicate an inline threadref (all zeros)
  // [24 .. 31]: thread (thread ref)
  EXPECT_EQ(inline_header & 0x0000'0000'FF00'0000, uint64_t{0x0000'0000'0000'0000});

  // We should see 2 extra words for the inline threadref
  uint64_t pid = inline_bytes[2];
  uint64_t tid = inline_bytes[3];
  EXPECT_EQ(pid, 0xDEADBEEF);
  EXPECT_EQ(tid, 0xCAFEF00D);
}

TEST(Serializer, IndexedArgumentNames) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category(2);
  fxt::StringRef name(3);
  fxt::StringRef arg_name(0x7FFF);

  FakeWriter indexed_writer;
  EXPECT_EQ(ZX_OK, fxt::WriteInstantEventRecord(&indexed_writer, event_time, thread_ref, category,
                                                name, fxt::Argument(arg_name)));

  // We should see one extra word for the argument header
  EXPECT_EQ(indexed_writer.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* indexed_bytes = reinterpret_cast<uint64_t*>(indexed_writer.bytes.data());
  uint64_t indexed_arg_header = indexed_bytes[2];

  // We expect our arg header to indicate an indexed stringref
  // [16 .. 31]: name (string ref)
  EXPECT_EQ(indexed_arg_header & 0x0000'0000'FFFF'0000, uint64_t{0x0000'0000'7FFF'0000});
}

TEST(Serializer, InlineArgumentNames) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category(2);
  fxt::StringRef name(3);
  fxt::StringRef arg_name_inline("argname");
  FakeWriter inline_writer;
  EXPECT_EQ(ZX_OK, fxt::WriteInstantEventRecord(&inline_writer, event_time, thread_ref, category,
                                                name, fxt::Argument(arg_name_inline)));

  // We should see one extra word for the argument header, and 1 for the inline string
  EXPECT_EQ(inline_writer.bytes.size(), fxt::WordSize(4).SizeInBytes());
  uint64_t* inline_bytes = reinterpret_cast<uint64_t*>(inline_writer.bytes.data());
  uint64_t inline_arg_header = inline_bytes[2];

  // We expect our arg header to indicate an inline stringref of length 7
  // [16 .. 31]: name (string ref)
  EXPECT_EQ(inline_arg_header & 0x0000'0000'FFFF'0000, uint64_t{0x0000'0000'8007'0000});

  char* name_stream = reinterpret_cast<char*>(inline_bytes + 3);
  EXPECT_EQ(std::memcmp(name_stream, "argname\0", 8), 0);
}

TEST(Serializer, Arguments) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category(2);
  fxt::StringRef name(3);

  fxt::StringRef arg_name(0x7FFF);

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteInstantEventRecord(
                       &writer, event_time, thread_ref, category, name, fxt::Argument(arg_name),
                       fxt::Argument(arg_name, true), fxt::Argument(arg_name, int32_t{0x12345678}),
                       fxt::Argument(arg_name, uint32_t{0x567890AB}),
                       fxt::Argument(arg_name, int64_t{0x1234'5678'90AB'CDEF}),
                       fxt::Argument<fxt::ArgumentType::kUint64, fxt::RefType::kId>(
                           arg_name, uint64_t{0xFEDC'BA09'8765'4321}),
                       fxt::Argument(arg_name, double{1234.5678}),
                       fxt::Argument<fxt::ArgumentType::kPointer, fxt::RefType::kId>(
                           arg_name, uintptr_t{0xDEADBEEF}),
                       fxt::Argument<fxt::ArgumentType::kKoid, fxt::RefType::kId>(
                           arg_name, zx_koid_t{0x12345678}),
                       fxt::Argument(arg_name, fxt::StringRef(11))));
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  // We should have 10 arguments
  uint64_t header = bytes[0];
  EXPECT_EQ(header & 0x0000'0000'00F0'0000, uint64_t{0x0000'0000'00A0'0000});

  const size_t num_words =
      1    // header
      + 1  // time stamp
      + 5  // 1 word for args that fit in the header (null, bool, int32, uint32, string arg (ref)
      + (2 * 5);  // 2 words for args that don't fit (int64, uint64, double, pointer, koid)
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(num_words).SizeInBytes());
  uint64_t null_arg_header = bytes[2];
  EXPECT_EQ(null_arg_header, uint64_t{0x0000'0000'7FFF'0010});

  uint64_t bool_arg_header = bytes[3];
  EXPECT_EQ(bool_arg_header, uint64_t{0x0000'0001'7FFF'0019});

  uint64_t int32_arg_header = bytes[4];
  EXPECT_EQ(int32_arg_header, uint64_t{0x1234'5678'7FFF'0011});

  uint64_t uint32_arg_header = bytes[5];
  EXPECT_EQ(uint32_arg_header, uint64_t{0x5678'90AB'7FFF'0012});

  uint64_t int64_arg_header = bytes[6];
  EXPECT_EQ(int64_arg_header, uint64_t{0x0000'0000'7FFF'0023});
  EXPECT_EQ(bytes[7], uint64_t{0x1234'5678'90AB'CDEF});

  uint64_t uint64_arg_header = bytes[8];
  EXPECT_EQ(uint64_arg_header, uint64_t{0x0000'0000'7FFF'0024});
  EXPECT_EQ(bytes[9], uint64_t{0xFEDC'BA09'8765'4321});

  uint64_t double_arg_header = bytes[10];
  EXPECT_EQ(double_arg_header, uint64_t{0x0000'0000'7FFF'0025});
  double exp_double_val = 1234.5678;
  EXPECT_EQ(bytes[11], *reinterpret_cast<uint64_t*>(&exp_double_val));

  uint64_t pointer_arg_header = bytes[12];
  EXPECT_EQ(pointer_arg_header, uint64_t{0x0000'0000'7FFF'0027});
  EXPECT_EQ(bytes[13], uint64_t{0xDEADBEEF});

  uint64_t koid_arg_header = bytes[14];
  EXPECT_EQ(koid_arg_header, uint64_t{0x0000'0000'7FFF'0028});
  EXPECT_EQ(bytes[15], uint64_t{0x12345678});

  uint64_t string_arg_header = bytes[16];
  EXPECT_EQ(string_arg_header, uint64_t{0x0000'000B'7FFF'0016});
}

TEST(Serializer, InstantEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);

  FakeWriter writer;
  EXPECT_EQ(ZX_OK,
            fxt::WriteInstantEventRecord(&writer, event_time, thread_ref, category_ref, name_ref));
  // One word for the header, one for the timestamp
  EXPECT_EQ(writer.bytes.size(), 16U);
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = bytes[0];
  // Event type should be 0
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0000'0000});
  // Timestamp should be correct
  EXPECT_EQ(bytes[1], event_time);
}

TEST(Serializer, CounterEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);
  fxt::StringRef arg_name(0x2345);

  FakeWriter writer;
  uint64_t counter_id = 0x334455'667788;
  EXPECT_EQ(ZX_OK,
            fxt::WriteCounterEventRecord(&writer, event_time, thread_ref, category_ref, name_ref,
                                         counter_id, fxt::Argument(arg_name, true)));
  // One word for the header, one for the timestamp, one for the counter, one for the argument
  EXPECT_EQ(writer.bytes.size(), 32U);
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = bytes[0];
  // Event type should be 0
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0001'0000});
  // Timestamp should be correct
  EXPECT_EQ(bytes[1], event_time);
  // The counter should come after the arguments
  EXPECT_EQ(bytes[3], counter_id);
}

}  // namespace
