// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxt/serializer.h>

#include <gtest/gtest.h>

#include "lib/fxt/fields.h"
#include "lib/fxt/record_types.h"
#include "zircon/syscalls/object.h"
#include "zircon/types.h"

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

    // In all codepaths, we expect that the number of bytes written exactly
    // matches the number of bytes for the record size indicated by the header.
    EXPECT_GE(bytes_.size(), sizeof(uint64_t));
    uint64_t header;
    memcpy(&header, bytes_.data(), sizeof(uint64_t));
    fxt::WordSize expected_size(0);
    if (fxt::RecordFields::Type::Get<fxt::RecordType>(header) == fxt::RecordType::kLargeRecord) {
      expected_size = fxt::WordSize(fxt::LargeRecordFields::RecordSize::Get<size_t>(header));
    } else {
      expected_size = fxt::WordSize(fxt::RecordFields::RecordSize::Get<size_t>(header));
    }
    EXPECT_EQ(bytes_.size(), expected_size.SizeInBytes());

    committed = true;
  }

  bool committed = false;
  std::vector<uint8_t>& bytes_;
};

struct FakeWriter {
  using Reservation = FakeRecord;
  std::vector<uint8_t> bytes;
  zx::result<FakeRecord> Reserve(uint64_t header) {
    FakeRecord rec{false, bytes};
    rec.WriteWord(header);
    return zx::ok(rec);
  }
};

struct FakeNoMemWriter {
  using Reservation = FakeRecord;
  zx::result<FakeRecord> Reserve(uint64_t header) { return zx::error(ZX_ERR_NO_MEMORY); }
};

TEST(Serializer, NoMemWriter) {
  FakeNoMemWriter writer_no_mem;
  EXPECT_EQ(ZX_ERR_NO_MEMORY, fxt::WriteInitializationRecord(&writer_no_mem, 0xABCD));
}

TEST(Serializer, ProviderInfoMetadataRecord) {
  FakeWriter writer_success;
  uint32_t provider_id = 0xAABBCCDD;
  const char* provider_name = "test_provider";
  size_t provider_name_length = strlen(provider_name);
  EXPECT_EQ(ZX_OK, fxt::WriteProviderInfoMetadataRecord(&writer_success, provider_id, provider_name,
                                                        provider_name_length));
  // 1 word header, 2 words name stream
  EXPECT_EQ(writer_success.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer_success.bytes.data());

  uint64_t header = bytes[0];
  // Record type of 0
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'0000});
  // 3 words in size
  EXPECT_EQ(header & 0x0000'0000'0000'FFF0, uint64_t{0x0000'0000'0000'0030});
  // Metadata type 1
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0001'0000});
  // Provider id
  EXPECT_EQ(header & 0x000F'FFFF'FFF0'0000, uint64_t{0x000A'ABBC'CDD0'0000});
  // Name length
  EXPECT_EQ(header & 0x0FF0'0000'0000'0000, uint64_t{0x00D0'0000'0000'0000});
  EXPECT_EQ(std::memcmp(bytes + 1, "test_pro", 8), 0);
  EXPECT_EQ(std::memcmp(bytes + 2, "vider\0\0\0", 8), 0);
}

TEST(Serializer, ProviderSectionMetadataRecord) {
  FakeWriter writer_success;
  uint32_t provider_id = 0xAABBCCDD;
  EXPECT_EQ(ZX_OK, fxt::WriteProviderSectionMetadataRecord(&writer_success, provider_id));
  // 1 word header
  EXPECT_EQ(writer_success.bytes.size(), fxt::WordSize(1).SizeInBytes());
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer_success.bytes.data());

  uint64_t header = bytes[0];
  // Record type of 0
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'0000});
  // 1 words in size
  EXPECT_EQ(header & 0x0000'0000'0000'FFF0, uint64_t{0x0000'0000'0000'0010});
  // Metadata type 2
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0002'0000});
  // Provider id
  EXPECT_EQ(header & 0x000F'FFFF'FFF0'0000, uint64_t{0x000A'ABBC'CDD0'0000});
}

TEST(Serializer, ProviderEventMetadataRecord) {
  FakeWriter writer_success;
  uint32_t provider_id = 0xAABBCCDD;
  uint8_t event_id = 0x7;
  EXPECT_EQ(ZX_OK, fxt::WriteProviderEventMetadataRecord(&writer_success, provider_id, event_id));
  // 1 word header
  EXPECT_EQ(writer_success.bytes.size(), fxt::WordSize(1).SizeInBytes());
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer_success.bytes.data());

  uint64_t header = bytes[0];
  // Record type of 0
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'0000});
  // 1 words in size
  EXPECT_EQ(header & 0x0000'0000'0000'FFF0, uint64_t{0x0000'0000'0000'0010});
  // Metadata type 3
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0003'0000});
  // Provider id
  EXPECT_EQ(header & 0x000F'FFFF'FFF0'0000, uint64_t{0x000A'ABBC'CDD0'0000});
  // Event Id
  EXPECT_EQ(header & 0x00F0'0000'0000'0000, uint64_t{0x0070'0000'0000'0000});
}

TEST(Serializer, MagicNumberMetadataRecord) {
  FakeWriter writer_success;
  EXPECT_EQ(ZX_OK, fxt::WriteMagicNumberRecord(&writer_success));
  // 1 word header
  EXPECT_EQ(writer_success.bytes.size(), fxt::WordSize(1).SizeInBytes());
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer_success.bytes.data());

  uint64_t header = bytes[0];
  // Record type of 0
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'0000});
  // 1 words in size
  EXPECT_EQ(header & 0x0000'0000'0000'FFF0, uint64_t{0x0000'0000'0000'0010});
  // Metadata type 4
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0004'0000});
  // Trace type info 0
  EXPECT_EQ(header & 0x0000'0000'00F0'0000, uint64_t{0x0000'0000'0000'0000});
  // FxT\16 in little endian
  EXPECT_EQ(header & 0x00FF'FFFF'FF00'0000, uint64_t{0x0016'5478'4600'0000});
  // Remainder is 0
  EXPECT_EQ(header & 0xFF00'0000'0000'0000, uint64_t{0x0000'0000'0000'0000});
}

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

TEST(Serializer, DurationBeginEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteDurationBeginEventRecord(&writer, event_time, thread_ref, category_ref,
                                                      name_ref));
  // One word for the header, one for the time stamp
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(2).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  // Event type should be 2
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0002'0000});
  EXPECT_EQ(words[1], event_time);
}

TEST(Serializer, DurationEndEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteDurationEndEventRecord(&writer, event_time, thread_ref, category_ref,
                                                    name_ref));
  // One word for the header, one for the time stamp
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(2).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  // Event type should be 3
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0003'0000});
  EXPECT_EQ(words[1], event_time);
}

TEST(Serializer, DurationCompleteEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  uint64_t event_end_time = 0x1122'3344'5566'7788;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteDurationCompleteEventRecord(&writer, event_time, thread_ref,
                                                         category_ref, name_ref, event_end_time));
  // One word for the header, one for the start time, one for the end time
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  // Event type should be 4
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0004'0000});
  EXPECT_EQ(words[1], event_time);
  EXPECT_EQ(words[2], event_end_time);
}

TEST(Serializer, AsyncBeginEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);
  uint64_t async_id = 0x1122'3344'5566'7788;

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteAsyncBeginEventRecord(&writer, event_time, thread_ref, category_ref,
                                                   name_ref, async_id));
  // One word for the header, one for the time stamp, one for the id
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0005'0000});
  EXPECT_EQ(words[1], event_time);
  EXPECT_EQ(words[2], async_id);
}

TEST(Serializer, AsyncInstantEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);
  uint64_t async_id = 0x1122'3344'5566'7788;

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteAsyncInstantEventRecord(&writer, event_time, thread_ref, category_ref,
                                                     name_ref, async_id));
  // One word for the header, one for the time stamp, one for the id
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0006'0000});
  EXPECT_EQ(words[1], event_time);
  EXPECT_EQ(words[2], async_id);
}

TEST(Serializer, AsyncEndEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);
  uint64_t async_id = 0x1122'3344'5566'7788;

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteAsyncEndEventRecord(&writer, event_time, thread_ref, category_ref,
                                                 name_ref, async_id));
  // One word for the header, one for the time stamp, one for the id
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0007'0000});
  EXPECT_EQ(words[1], event_time);
  EXPECT_EQ(words[2], async_id);
}

TEST(Serializer, FlowBeginEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);
  uint64_t flow_id = 0x1122'3344'5566'7788;

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteFlowBeginEventRecord(&writer, event_time, thread_ref, category_ref,
                                                  name_ref, flow_id));
  // One word for the header, one for the time stamp, one for the id
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0008'0000});
  EXPECT_EQ(words[1], event_time);
  EXPECT_EQ(words[2], flow_id);
}

TEST(Serializer, FlowInstantEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);
  uint64_t flow_id = 0x1122'3344'5566'7788;

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteFlowStepEventRecord(&writer, event_time, thread_ref, category_ref,
                                                 name_ref, flow_id));
  // One word for the header, one for the time stamp, one for the id
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'0009'0000});
  EXPECT_EQ(words[1], event_time);
  EXPECT_EQ(words[2], flow_id);
}

TEST(Serializer, FlowEndEventRecord) {
  uint64_t event_time = 0x1234'5678'90AB'CDEF;
  fxt::ThreadRef thread_ref(1);
  fxt::StringRef category_ref(0x7777);
  fxt::StringRef name_ref(0x1234);
  uint64_t flow_id = 0x1122'3344'5566'7788;

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteFlowEndEventRecord(&writer, event_time, thread_ref, category_ref,
                                                name_ref, flow_id));
  // One word for the header, one for the time stamp, one for the id
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  EXPECT_EQ(header & 0x0000'0000'000F'0000, uint64_t{0x0000'0000'000A'0000});
  EXPECT_EQ(words[1], event_time);
  EXPECT_EQ(words[2], flow_id);
}

TEST(Serializer, BlobRecord) {
  fxt::StringRef blob_name(0x7777);
  fxt::BlobType type = fxt::BlobType::kData;
  const char* data = "This is some data that we are writing";
  size_t num_bytes = strlen(data);  // 37

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteBlobRecord(&writer, blob_name, type,
                                        reinterpret_cast<const void*>(data), num_bytes));
  // One word for the header, five for the data
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(6).SizeInBytes());
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = bytes[0];
  // Record type is 5
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'0005});
  // Size
  EXPECT_EQ(header & 0x0000'0000'0000'FFF0, uint64_t{0x0000'0000'0000'0060});
  // Block name ref
  EXPECT_EQ(header & 0x0000'0000'FFFF'0000, uint64_t{0x0000'0000'7777'0000});
  // Blob size
  EXPECT_EQ(header & 0x0000'7FFF'0000'0000, uint64_t{0x0000'0025'0000'0000});
  // Type
  EXPECT_EQ(header & 0x00FF'0000'0000'0000, uint64_t{0x0001'0000'0000'0000});
  EXPECT_EQ(std::memcmp(bytes + 1, "This is ", 8), 0);
  EXPECT_EQ(std::memcmp(bytes + 2, "some dat", 8), 0);
  EXPECT_EQ(std::memcmp(bytes + 3, "a that w", 8), 0);
  EXPECT_EQ(std::memcmp(bytes + 4, "e are wr", 8), 0);
  EXPECT_EQ(std::memcmp(bytes + 5, "iting\0\0\0", 8), 0);
}

TEST(Serializer, UserspaceObjectRecord) {
  fxt::StringRef name(0x7777);
  fxt::StringRef arg_name(0x1234);
  fxt::ThreadRef thread(0xAA);
  uintptr_t ptr = 0xDEADBEEF;

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteUserspaceObjectRecord(&writer, ptr, thread, name,
                                                   fxt::Argument(arg_name, true)));
  // 1 word for the header, 1 for the pointer, 1 for the argument
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = bytes[0];
  // Record type is 6
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'0006});
  // Size
  EXPECT_EQ(header & 0x0000'0000'0000'FFF0, uint64_t{0x0000'0000'0000'0030});
  // Threadref
  EXPECT_EQ(header & 0x0000'0000'00FF'0000, uint64_t{0x0000'0000'00AA'0000});
  // Name Ref
  EXPECT_EQ(header & 0x0000'00FF'FF00'0000, uint64_t{0x0000'0077'7700'0000});
  EXPECT_EQ(bytes[1], ptr);
  // Argument (true)
  EXPECT_EQ(bytes[2], uint64_t{0x0000'0001'1234'0019});
}

TEST(Serializer, KernelObjectRecord) {
  fxt::StringRef name(0x7777);
  fxt::StringRef arg_name(0x4321);
  zx_koid_t koid = 0xDEADBEEF;
  zx_obj_type_t obj_type = ZX_OBJ_TYPE_CHANNEL;

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteKernelObjectRecord(&writer, koid, obj_type, name,
                                                fxt::Argument(arg_name, false)));
  // 1 word for the header, 1 for the pointer, 1 for the argument
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(3).SizeInBytes());
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = bytes[0];
  // Record type is 7
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'0007});
  // Size
  EXPECT_EQ(header & 0x0000'0000'0000'FFF0, uint64_t{0x0000'0000'0000'0030});
  // Obj type
  EXPECT_EQ(header & 0x0000'0000'00FF'0000, uint64_t{0x0000'0000'0004'0000});
  // Name Ref
  EXPECT_EQ(header & 0x0000'00FF'FF00'0000, uint64_t{0x0000'0077'7700'0000});
  EXPECT_EQ(bytes[1], koid);
  // Argument (true)
  EXPECT_EQ(bytes[2], uint64_t{0x0000'0000'4321'0019});
}

TEST(Serializer, KernelObjectRecordInlineNames) {
  fxt::StringRef name("name");
  fxt::StringRef arg_name("arg");
  zx_koid_t koid = 0xDEADBEEF;
  zx_obj_type_t obj_type = ZX_OBJ_TYPE_CHANNEL;

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteKernelObjectRecord(&writer, koid, obj_type, name,
                                                fxt::Argument(arg_name, false)));
  // 1 word for the header, 1 for the pointer, 1 for the argument
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(5).SizeInBytes());
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = bytes[0];
  // Record type is 7
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'0007});
  // Size
  EXPECT_EQ(header & 0x0000'0000'0000'FFF0, uint64_t{0x0000'0000'0000'0050});
  // Obj type
  EXPECT_EQ(header & 0x0000'0000'00FF'0000, uint64_t{0x0000'0000'0004'0000});
  // Name Ref
  EXPECT_EQ(header & 0x0000'00FF'FF00'0000, uint64_t{0x0000'0080'0400'0000});
  EXPECT_EQ(bytes[1], koid);
  EXPECT_EQ(bytes[2], uint64_t{0x0000'0000'656d'616e});
  // Argument (true)
  EXPECT_EQ(bytes[3], uint64_t{0x0000'0000'8003'0029});
  EXPECT_EQ(bytes[4], uint64_t{0x0000'0000'0067'7261});
}

TEST(Serializer, ContextSwitchRecord) {
  uint64_t event_time = 0xAABB'CCDD'EEFF'0011;
  uint8_t cpu_number = 0xBB;
  zx_thread_state_t outgoing_state = ZX_THREAD_STATE_SUSPENDED;
  fxt::ThreadRef outgoing_thread(0x1);
  fxt::ThreadRef incoming_thread(0x2);
  uint8_t outgoing_thread_pri = 3;
  uint8_t incoming_thread_pri = 4;

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteContextSwitchRecord(&writer, event_time, cpu_number, outgoing_state,
                                                 outgoing_thread, incoming_thread,
                                                 outgoing_thread_pri, incoming_thread_pri));
  // 1 word for the header, 1 for the timestamp
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(2).SizeInBytes());
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = bytes[0];
  // Record type is 8
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'0008});
  // Size
  EXPECT_EQ(header & 0x0000'0000'0000'FFF0, uint64_t{0x0000'0000'0000'0020});
  // CPU number
  EXPECT_EQ(header & 0x0000'0000'00FF'0000, uint64_t{0x0000'0000'00BB'0000});
  // State
  EXPECT_EQ(header & 0x0000'0000'0F00'0000, uint64_t{0x0000'0000'0200'0000});
  // out ref
  EXPECT_EQ(header & 0x0000'000F'F000'0000, uint64_t{0x0000'0000'1000'0000});
  // in ref
  EXPECT_EQ(header & 0x0000'0FF0'0000'0000, uint64_t{0x0000'0020'0000'0000});
  // out pri
  EXPECT_EQ(header & 0x000F'F000'0000'0000, uint64_t{0x0000'3000'0000'0000});
  // in pri
  EXPECT_EQ(header & 0x0FF0'0000'0000'0000, uint64_t{0x0040'0000'0000'0000});
  EXPECT_EQ(bytes[1], event_time);
}

TEST(Serializer, LogRecord) {
  uint64_t event_time = 0xAABB'CCDD'EEFF'0011;
  fxt::ThreadRef log_thread(0x1);
  const char* message = "This is a log message";
  size_t message_len = strlen(message);  // 21

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteLogRecord(&writer, event_time, log_thread, message, message_len));
  // 1 word for the header, 1 for the timestamp, 3 words for the message
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(5).SizeInBytes());
  uint64_t* bytes = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = bytes[0];
  // Record type is 9
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'0009});
  // Size
  EXPECT_EQ(header & 0x0000'0000'0000'FFF0, uint64_t{0x0000'0000'0000'0050});
  // message length
  EXPECT_EQ(header & 0x0000'0000'8FFF'0000, uint64_t{0x0000'0000'0015'0000});
  // thread_ref
  EXPECT_EQ(header & 0x0000'00FF'0000'0000, uint64_t{0x0000'0001'0000'0000});
  EXPECT_EQ(bytes[1], event_time);
  EXPECT_EQ(std::memcmp(bytes + 2, "This is ", 8), 0);
  EXPECT_EQ(std::memcmp(bytes + 3, "a log me", 8), 0);
  EXPECT_EQ(std::memcmp(bytes + 4, "ssage\0\0\0", 8), 0);
}

TEST(Serializer, LargeBlobWithMetadataRecord) {
  uint64_t event_time = 0xAABB'CCDD'EEFF'0011;
  fxt::StringRef category_ref(0x7AAA);
  fxt::StringRef name_ref(0x7BBB);
  fxt::ThreadRef thread_ref(0xCC);
  fxt::StringRef arg_name(0x2345);
  const char* data = "Some data to write into the buffer";
  size_t blob_size_bytes = strlen(data);  // 34

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteLargeBlobRecordWithMetadata(
                       &writer, event_time, category_ref, name_ref, thread_ref, data,
                       blob_size_bytes, fxt::Argument(arg_name, true)));

  // 1 word for the large header, 1 for the blob header, 1 for timestamp, 1 for
  // the argument header, 1 for blob size, 5 for payload.
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(10).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  // Record type is 15
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'000F});
  // Size
  EXPECT_EQ(header & 0x0000'000F'FFFF'FFF0, uint64_t{0x0000'0000'0000'00A0});
  // large record type (0)
  EXPECT_EQ(header & 0x0000'00F0'0000'0000, uint64_t{0x0000'0000'0000'0000});
  // blob format type (0)
  EXPECT_EQ(header & 0x0000'0F00'0000'0000, uint64_t{0x0000'0000'0000'0000});
  uint64_t blob_header = words[1];
  // Category Ref
  EXPECT_EQ(blob_header & 0x0000'0000'0000'FFFF, uint64_t{0x0000'0000'0000'7AAA});
  // Name Ref
  EXPECT_EQ(blob_header & 0x0000'0000'FFFF'0000, uint64_t{0x0000'0000'7BBB'0000});
  // thread
  EXPECT_EQ(blob_header & 0x0000'0FF0'0000'0000, uint64_t{0x0000'0CC0'0000'0000});

  EXPECT_EQ(words[2], event_time);
  // Argument
  EXPECT_EQ(words[3], uint64_t{0x0000'0001'2345'0019});
  EXPECT_EQ(words[4], blob_size_bytes);
  EXPECT_EQ(std::memcmp(words + 5, "Some dat", 8), 0);
  EXPECT_EQ(std::memcmp(words + 6, "a to wri", 8), 0);
  EXPECT_EQ(std::memcmp(words + 7, "te into ", 8), 0);
  EXPECT_EQ(std::memcmp(words + 8, "the buff", 8), 0);
  EXPECT_EQ(std::memcmp(words + 9, "er\0\0\0\0\0\0", 8), 0);
}

TEST(Serializer, LargeBlobWithNoMetadataRecord) {
  fxt::StringRef category_ref(0x7AAA);
  fxt::StringRef name_ref(0x7BBB);
  const char* data = "Some data to write into the buffer";
  size_t blob_size_bytes = strlen(data);  // 34

  FakeWriter writer;
  EXPECT_EQ(ZX_OK, fxt::WriteLargeBlobRecordWithNoMetadata(&writer, category_ref, name_ref, data,
                                                           blob_size_bytes));
  // 1 word for the large header, 1 for the blob header, 1 for
  // blob size, 5 for payload.
  EXPECT_EQ(writer.bytes.size(), fxt::WordSize(8).SizeInBytes());
  uint64_t* words = reinterpret_cast<uint64_t*>(writer.bytes.data());
  uint64_t header = words[0];
  // Record type is 15
  EXPECT_EQ(header & 0x0000'0000'0000'000F, uint64_t{0x0000'0000'0000'000F});
  // Size
  EXPECT_EQ(header & 0x0000'000F'FFFF'FFF0, uint64_t{0x0000'0000'0000'0080});
  // large record type (0)
  EXPECT_EQ(header & 0x0000'00F0'0000'0000, uint64_t{0x0000'0000'0000'0000});
  // blob format type (0)
  EXPECT_EQ(header & 0x0000'0F00'0000'0000, uint64_t{0x0000'0100'0000'0000});
  uint64_t blob_header = words[1];
  // Category Ref
  EXPECT_EQ(blob_header & 0x0000'0000'0000'FFFF, uint64_t{0x0000'0000'0000'7AAA});
  // Name Ref
  EXPECT_EQ(blob_header & 0x0000'0000'FFFF'0000, uint64_t{0x0000'0000'7BBB'0000});

  EXPECT_EQ(words[2], blob_size_bytes);
  EXPECT_EQ(std::memcmp(words + 3, "Some dat", 8), 0);
  EXPECT_EQ(std::memcmp(words + 4, "a to wri", 8), 0);
  EXPECT_EQ(std::memcmp(words + 5, "te into ", 8), 0);
  EXPECT_EQ(std::memcmp(words + 6, "the buff", 8), 0);
  EXPECT_EQ(std::memcmp(words + 7, "er\0\0\0\0\0\0", 8), 0);
}

}  // namespace
