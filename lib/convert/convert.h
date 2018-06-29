// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_CONVERT_CONVERT_H_
#define PERIDOT_LIB_CONVERT_CONVERT_H_

#include <string>

#include <flatbuffers/flatbuffers.h>
#include <leveldb/db.h>
#include <rapidjson/document.h>

#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/vector.h"
#include "lib/fxl/strings/string_view.h"

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
class ExtendedStringView : public fxl::StringView {
 public:
  ExtendedStringView(const std::vector<uint8_t>& array)  // NOLINT
      : fxl::StringView(reinterpret_cast<const char*>(array.data()),
                        array.size()) {}
  ExtendedStringView(const fidl::VectorPtr<uint8_t>& array)  // NOLINT
      : fxl::StringView(reinterpret_cast<const char*>(array->data()),
                        array->size()) {}
  template <size_t N>
  constexpr ExtendedStringView(const fidl::Array<uint8_t, N>& array)  // NOLINT
      : fxl::StringView(reinterpret_cast<const char*>(array.data()), N) {}
  ExtendedStringView(const leveldb::Slice& slice)  // NOLINT
      : fxl::StringView(slice.data(), slice.size()) {}
  ExtendedStringView(const std::string& string)  // NOLINT
      : fxl::StringView(string) {}
  constexpr ExtendedStringView(fxl::StringView string_view)  // NOLINT
      : fxl::StringView(string_view) {}
  ExtendedStringView(const rapidjson::Value& value)  // NOLINT
      : fxl::StringView(value.GetString(), value.GetStringLength()) {
    FXL_DCHECK(value.IsString());
  }
  template <size_t N>
  constexpr ExtendedStringView(const char (&str)[N])  // NOLINT
      : fxl::StringView(str) {}
  ExtendedStringView(  // NOLINT
      const flatbuffers::Vector<uint8_t>* byte_storage)
      : fxl::StringView(reinterpret_cast<const char*>(byte_storage->data()),
                        byte_storage->size()) {}
  ExtendedStringView(  // NOLINT
      const flatbuffers::FlatBufferBuilder& buffer_builder)
      : fxl::StringView(
            reinterpret_cast<char*>(buffer_builder.GetBufferPointer()),
            buffer_builder.GetSize()) {}

  operator leveldb::Slice() const {  // NOLINT
    return leveldb::Slice(data(), size());
  }

  fidl::VectorPtr<uint8_t> ToArray();

  flatbuffers::Offset<flatbuffers::Vector<uint8_t>> ToFlatBufferVector(
      flatbuffers::FlatBufferBuilder* builder);

  std::string ToHex();
};

// Returns the ExtendedStringView representation of the given value.
inline ExtendedStringView ToStringView(ExtendedStringView value) {
  return value;
}

// Returns the representation of the given value in LevelDB.
inline leveldb::Slice ToSlice(ExtendedStringView value) { return value; }

// Returns the fidl::VectorPtr representation of the given value.
fidl::VectorPtr<uint8_t> ToArray(ExtendedStringView value);

// Returns the fidl::Array representation of the given value.
template <size_t N>
void ToArray(ExtendedStringView value, fidl::Array<uint8_t, N>* out) {
  ZX_ASSERT(value.size() == N);
  memcpy(out->mutable_data(), value.data(), N);
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
  bool operator()(const std::string& lhs, const std::string& rhs) const {
    return lhs < rhs;
  }
  bool operator()(ExtendedStringView lhs, const std::string& rhs) const {
    return lhs < fxl::StringView(rhs);
  }
  bool operator()(const std::string& lhs, ExtendedStringView rhs) const {
    return fxl::StringView(lhs) < rhs;
  }
};

}  // namespace convert

#endif  // PERIDOT_LIB_CONVERT_CONVERT_H_
