// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Record field declarations for FXT as defined in
// docs/reference/tracing/trace-format.md

#ifndef SRC_LIB_FXT_INCLUDE_LIB_FXT_FIELDS_H_
#define SRC_LIB_FXT_INCLUDE_LIB_FXT_FIELDS_H_

#include <cstdint>
#include <type_traits>

#include "record_types.h"

namespace fxt {

// Casts an enum's value to its underlying type.
template <typename T>
inline constexpr typename std::underlying_type<T>::type ToUnderlyingType(T value) {
  return static_cast<typename std::underlying_type<T>::type>(value);
}

// Describes the layout of a bit-field packed into a 64-bit word.
template <size_t begin, size_t end>
struct Field {
  static_assert(begin < sizeof(uint64_t) * 8, "begin is out of bounds");
  static_assert(end < sizeof(uint64_t) * 8, "end is out of bounds");
  static_assert(begin <= end, "begin must not be larger than end");
  static_assert(end - begin + 1 < 64, "must be a part of a word, not a whole word");

  static constexpr uint64_t kMask = (uint64_t{1} << (end - begin + 1)) - 1;

  static constexpr uint64_t Make(uint64_t value) { return (value & kMask) << begin; }

  template <typename U>
  static constexpr U Get(uint64_t word) {
    static_assert(sizeof(U) * 8 >= end - begin + 1, "type must fit all the bits");
    return static_cast<U>((word >> begin) & kMask);
  }

  static constexpr void Set(uint64_t& word, uint64_t value) {
    word = (word & ~(kMask << begin)) | Make(value);
  }
};

struct ArgumentFields {
  using Type = Field<0, 3>;
  using ArgumentSize = Field<4, 15>;
  using NameRef = Field<16, 31>;
};

struct BoolArgumentFields : ArgumentFields {
  using Value = Field<32, 32>;
};

struct Int32ArgumentFields : ArgumentFields {
  using Value = Field<32, 63>;
};

struct Uint32ArgumentFields : ArgumentFields {
  using Value = Field<32, 63>;
};

struct StringArgumentFields : ArgumentFields {
  using Index = Field<32, 47>;
};

struct RecordFields {
  static constexpr uint64_t kMaxRecordSizeWords = 0xfff;
  static constexpr uint64_t kMaxRecordSizeBytes = WordSize(kMaxRecordSizeWords).SizeInBytes();

  using Type = Field<0, 3>;
  using RecordSize = Field<4, 15>;
};

struct LargeRecordFields {
  static constexpr uint64_t kMaxRecordSizeWords = (1ull << 32) - 1;
  static constexpr uint64_t kMaxRecordSizeBytes = WordSize(kMaxRecordSizeWords).SizeInBytes();

  using Type = Field<0, 3>;
  using RecordSize = Field<4, 35>;
  using LargeType = Field<36, 39>;
};

struct MetadataRecordFields : RecordFields {
  using MetadataType = Field<16, 19>;
};

struct ProviderInfoMetadataRecordFields : MetadataRecordFields {
  static constexpr size_t kMaxNameLength = 0xff;

  using Id = Field<20, 51>;
  using NameLength = Field<52, 59>;
};

struct ProviderSectionMetadataRecordFields : MetadataRecordFields {
  using Id = Field<20, 51>;
};

struct ProviderEventMetadataRecordFields : MetadataRecordFields {
  using Id = Field<20, 51>;
  using Event = Field<52, 55>;
};

struct TraceInfoMetadataRecordFields : MetadataRecordFields {
  using TraceInfoType = Field<20, 23>;
};

struct MagicNumberRecordFields : TraceInfoMetadataRecordFields {
  using Magic = Field<24, 55>;
};

using InitializationRecordFields = RecordFields;

struct StringRecordFields : RecordFields {
  using StringIndex = Field<16, 30>;
  using StringLength = Field<32, 46>;
};

struct ThreadRecordFields : RecordFields {
  using ThreadIndex = Field<16, 23>;
};

struct EventRecordFields : RecordFields {
  using EventType = Field<16, 19>;
  using ArgumentCount = Field<20, 23>;
  using ThreadRef = Field<24, 31>;
  using CategoryStringRef = Field<32, 47>;
  using NameStringRef = Field<48, 63>;
};

struct BlobRecordFields : RecordFields {
  using NameStringRef = Field<16, 31>;
  using BlobSize = Field<32, 46>;
  using BlobType = Field<48, 55>;
};

struct UserspaceObjectRecordFields : RecordFields {
  using ProcessThreadRef = Field<16, 23>;
  using NameStringRef = Field<24, 39>;
  using ArgumentCount = Field<40, 43>;
};

struct KernelObjectRecordFields : RecordFields {
  using ObjectType = Field<16, 23>;
  using NameStringRef = Field<24, 39>;
  using ArgumentCount = Field<40, 43>;
};

struct ContextSwitchRecordFields : RecordFields {
  using CpuNumber = Field<16, 23>;
  using OutgoingThreadState = Field<24, 27>;
  using OutgoingThreadRef = Field<28, 35>;
  using IncomingThreadRef = Field<36, 43>;
  using OutgoingThreadPriority = Field<44, 51>;
  using IncomingThreadPriority = Field<52, 59>;
};

struct LogRecordFields : RecordFields {
  static constexpr size_t kMaxMessageLength = 0x7fff;
  using LogMessageLength = Field<16, 30>;
  using ThreadRef = Field<32, 39>;
};

struct LargeBlobFields : LargeRecordFields {
  using BlobFormat = Field<40, 43>;
};

struct BlobFormatAttachmentFields {
  using CategoryStringRef = Field<0, 15>;
  using NameStringRef = Field<16, 31>;
};

struct BlobFormatEventFields {
  using CategoryStringRef = Field<0, 15>;
  using NameStringRef = Field<16, 31>;
  using ArgumentCount = Field<32, 35>;
  using ThreadRef = Field<36, 43>;
};

}  // namespace fxt

#endif  // SRC_LIB_FXT_INCLUDE_LIB_FXT_FIELDS_H_
