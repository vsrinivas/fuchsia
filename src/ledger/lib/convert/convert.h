// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_CONVERT_CONVERT_H_
#define SRC_LEDGER_LIB_CONVERT_CONVERT_H_

#include <lib/fidl/cpp/vector.h>

#include <array>
#include <string>

#include <leveldb/db.h>

#include "third_party/abseil-cpp/absl/strings/string_view.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace convert {

// Provides conversions between fidl::VectorPtr, leveldb::Slice and std::string
// representations of a data object.
//
// This class doesn't take ownership of the data used to construct it. The data
// must outlive it. It is used to allow transparent handling of FIDL arrays,
// leveldb slices and strings.
//
// Single-argument constructors and conversion operators are marked as NOLINT to
// suppress `google-explicit-constructor` clang-tidy warning - in this case the
// implicit conversion is intended.
class ExtendedStringView : public absl::string_view {
 public:
  ExtendedStringView(const std::vector<uint8_t>& array)  // NOLINT
      : absl::string_view(reinterpret_cast<const char*>(array.data()), array.size()) {}
  ExtendedStringView(const fidl::VectorPtr<uint8_t>& array)  // NOLINT
      : absl::string_view(reinterpret_cast<const char*>(array->data()), array->size()) {}
  template <size_t N>
  constexpr ExtendedStringView(const std::array<uint8_t, N>& array)  // NOLINT
      : absl::string_view(reinterpret_cast<const char*>(array.data()), N) {}
  ExtendedStringView(const leveldb::Slice& slice)  // NOLINT
      : absl::string_view(slice.data(), slice.size()) {}
  ExtendedStringView(const std::string& string)  // NOLINT
      : absl::string_view(string) {}
  constexpr ExtendedStringView(absl::string_view string_view)  // NOLINT
      : absl::string_view(string_view) {}
  template <size_t N>
  constexpr ExtendedStringView(const char (&str)[N])  // NOLINT
      : absl::string_view(str) {}
  ExtendedStringView(  // NOLINT
      const flatbuffers::Vector<uint8_t>* byte_storage)
      : absl::string_view(reinterpret_cast<const char*>(byte_storage->data()),
                          byte_storage->size()) {}
  ExtendedStringView(  // NOLINT
      const flatbuffers::FlatBufferBuilder& buffer_builder)
      : absl::string_view(reinterpret_cast<char*>(buffer_builder.GetBufferPointer()),
                          buffer_builder.GetSize()) {}

  operator leveldb::Slice() const {  // NOLINT
    return leveldb::Slice(data(), size());
  }

  std::vector<uint8_t> ToArray();

  flatbuffers::Offset<flatbuffers::Vector<uint8_t>> ToFlatBufferVector(
      flatbuffers::FlatBufferBuilder* builder);

  std::string ToHex();
};

// Returns the ExtendedStringView representation of the given value.
inline ExtendedStringView ToStringView(ExtendedStringView value) { return value; }

// Returns the representation of the given value in LevelDB.
inline leveldb::Slice ToSlice(ExtendedStringView value) { return value; }

// Returns the std::vector representation of the given value.
std::vector<uint8_t> ToArray(ExtendedStringView value);

// Returns the std::array representation of the given value.
template <size_t N>
void ToArray(ExtendedStringView value, std::array<uint8_t, N>* out) {
  ZX_ASSERT(value.size() == N);
  memcpy(out->data(), value.data(), N);
}

// Returns the std::string representation of the given value.
std::string ToString(ExtendedStringView value);

// Returns the hexadecimal representation of the given value.
std::string ToHex(ExtendedStringView value);

// Store the given value as a FlatBufferVector in the given builder.
flatbuffers::Offset<flatbuffers::Vector<uint8_t>> ToFlatBufferVector(
    flatbuffers::FlatBufferBuilder* builder, ExtendedStringView value);

// Comparator that allows heterogeneous lookup by StringView and
// std::string in a container with the key type of std::string.
struct StringViewComparator {
  using is_transparent = std::true_type;
  bool operator()(const std::string& lhs, const std::string& rhs) const { return lhs < rhs; }
  bool operator()(ExtendedStringView lhs, const std::string& rhs) const {
    return lhs < absl::string_view(rhs);
  }
  bool operator()(const std::string& lhs, ExtendedStringView rhs) const {
    return absl::string_view(lhs) < rhs;
  }
};

}  // namespace convert

#endif  // SRC_LEDGER_LIB_CONVERT_CONVERT_H_
