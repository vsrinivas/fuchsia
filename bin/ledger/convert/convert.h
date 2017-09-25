// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_CONVERT_CONVERT_H_
#define PERIDOT_BIN_LEDGER_CONVERT_CONVERT_H_

#include <leveldb/db.h>
#include <rapidjson/document.h>
#include <string>

#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fxl/strings/string_view.h"
#include "peridot/bin/ledger/convert/bytes_generated.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace convert {

// Provides conversions between fidl::Array, leveldb::Slice and std::string
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
  ExtendedStringView(const fidl::Array<uint8_t>& array)  // NOLINT
      : fxl::StringView(reinterpret_cast<const char*>(array.data()),
                        array.size()) {}
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
  ExtendedStringView(const IdStorage* id_storage)  // NOLINT
      : fxl::StringView(reinterpret_cast<const char*>(id_storage),
                        sizeof(IdStorage)) {}

  operator leveldb::Slice() const {  // NOLINT
    return leveldb::Slice(data(), size());
  }
  operator const IdStorage*() const {  // NOLINT
    FXL_DCHECK(size() == sizeof(IdStorage));
    return reinterpret_cast<const IdStorage*>(data());
  }

  fidl::Array<uint8_t> ToArray();

  flatbuffers::Offset<flatbuffers::Vector<uint8_t>> ToFlatBufferVector(
      flatbuffers::FlatBufferBuilder* builder);

  std::string ToHex();
};

// Returns the ExtendedStringView representation of the given value.
inline ExtendedStringView ToStringView(ExtendedStringView value) {
  return value;
}

// Returns the representation of the given value in LevelDB.
inline leveldb::Slice ToSlice(ExtendedStringView value) {
  return value;
}

// Returns the representation of the given value as an IdStorage.
inline const IdStorage* ToIdStorage(ExtendedStringView value) {
  return value;
}

// Returns the fidl::Array representation of the given value.
fidl::Array<uint8_t> ToArray(ExtendedStringView value);

// Returns the std::string representation of the given value.
std::string ToString(ExtendedStringView value);

// Returns the hexadecimal representation of the given value.
std::string ToHex(ExtendedStringView value);

// Store the given value as a FlatBufferVector in the given builder.
flatbuffers::Offset<flatbuffers::Vector<uint8_t>> ToFlatBufferVector(
    flatbuffers::FlatBufferBuilder* builder,
    ExtendedStringView value);

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

#endif  // PERIDOT_BIN_LEDGER_CONVERT_CONVERT_H_
