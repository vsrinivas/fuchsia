// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CONVERT_CONVERT_H_
#define APPS_LEDGER_SRC_CONVERT_CONVERT_H_

#include <leveldb/db.h>
#include <rapidjson/document.h>
#include <string>

#include "apps/ledger/src/convert/bytes_generated.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/ftl/strings/string_view.h"
#include "third_party/flatbuffers/include/flatbuffers/flatbuffers.h"

namespace convert {

// Provides conversions between fidl::Array, leveldb::Slice and std::string
// representations of a data object.

// This class doesn't take ownership of the data used to construct it. The data
// must outlive it. It is used to allow transparent handling of FIDL arrays,
// leveldb slices and strings.
class ExtendedStringView : public ftl::StringView {
 public:
  ExtendedStringView(const fidl::Array<uint8_t>& array)
      : ftl::StringView(reinterpret_cast<const char*>(array.data()),
                        array.size()) {}
  ExtendedStringView(const leveldb::Slice& slice)
      : ftl::StringView(slice.data(), slice.size()) {}
  ExtendedStringView(const std::string& string) : ftl::StringView(string) {}
  constexpr ExtendedStringView(ftl::StringView string_view)
      : ftl::StringView(string_view) {}
  ExtendedStringView(const rapidjson::Value& value)
      : ftl::StringView(value.GetString(), value.GetStringLength()) {
    FTL_DCHECK(value.IsString());
  }
  template <size_t N>
  constexpr ExtendedStringView(const char (&str)[N]) : ftl::StringView(str) {}
  ExtendedStringView(const flatbuffers::Vector<uint8_t>* byte_storage)
      : ftl::StringView(reinterpret_cast<const char*>(byte_storage->data()),
                        byte_storage->size()) {}
  ExtendedStringView(const IdStorage* id_storage)
      : ftl::StringView(reinterpret_cast<const char*>(id_storage),
                        sizeof(IdStorage)) {}

  operator leveldb::Slice() const { return leveldb::Slice(data(), size()); }
  operator const IdStorage*() const {
    FTL_DCHECK(size() == sizeof(IdStorage));
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
    return lhs < ftl::StringView(rhs);
  }
  bool operator()(const std::string& lhs, ExtendedStringView rhs) const {
    return ftl::StringView(lhs) < rhs;
  }
};

}  // namespace convert

#endif  // APPS_LEDGER_SRC_CONVERT_CONVERT_H_
