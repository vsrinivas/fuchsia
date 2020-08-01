// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYSLOG_STREAMS_CPP_FIELDS_H_

#define LIB_SYSLOG_STREAMS_CPP_FIELDS_H_
#include <vector>

template <size_t begin, size_t end>
struct Field final {
  static_assert(begin < sizeof(uint64_t) * 8, "begin is out of bounds");
  static_assert(end < sizeof(uint64_t) * 8, "end is out of bounds");
  static_assert(begin <= end, "begin must not be larger than end");
  static_assert(end - begin + 1 < 64, "must be a part of a word, not a whole word");

  static constexpr uint64_t kMask = (uint64_t(1) << (end - begin + 1)) - 1;

  template <typename T>
  static constexpr uint64_t Make(T value) {
    return static_cast<uint64_t>(value) << begin;
  }

  template <typename U>
  static constexpr U Get(uint64_t word) {
    return static_cast<U>((word >> (begin % 64)) & kMask);
  }

  static constexpr void Set(uint64_t* word, uint64_t value) {
    *word = (*word & ~(kMask << begin)) | (value << begin);
  }
};

// HeaderField structure for a Record
struct HeaderFields {
  using Type = Field<0, 3>;
  using SizeWords = Field<4, 15>;
  using Reserved = Field<16, 55>;
  using Severity = Field<56, 63>;
};

// TODO(rminocha): Check ordering of MSB for little-endian
// ArgumentField structure for an Argument
struct ArgumentFields {
  using Type = Field<0, 3>;
  using SizeWords = Field<4, 15>;
  using NameRefVal = Field<16, 30>;
  using NameRefMSB = Field<31, 31>;
  using ValueRef = Field<32, 47>;
  using Reserved = Field<32, 63>;
};
#endif  // LIB_SYSLOG_STREAMS_CPP_FIELDS_H_
