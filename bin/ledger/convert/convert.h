// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_CONVERT_CONVERT_H_
#define APPS_LEDGER_SRC_CONVERT_CONVERT_H_

#include <leveldb/db.h>
#include <string>

#include "lib/ftl/strings/string_view.h"
#include "mojo/public/cpp/bindings/array.h"

namespace convert {

// Provides conversions between mojo::Array, leveldb::Slice and std::string
// representations of a data object.

// This class doesn't take ownership of the data used to construct it. The data
// must outlive it. It is used to allow transparent handling of mojo arrays,
// leveldb slices and strings.
class ExtendedStringView : public ftl::StringView {
 public:
  ExtendedStringView(const mojo::Array<uint8_t>& array)
      : ftl::StringView(reinterpret_cast<const char*>(array.data()),
                        array.size()) {}
  ExtendedStringView(const leveldb::Slice& slice)
      : ftl::StringView(slice.data(), slice.size()) {}
  ExtendedStringView(const std::string& string) : ftl::StringView(string) {}
  constexpr ExtendedStringView(ftl::StringView string_view)
      : ftl::StringView(string_view) {}
  template <size_t N>
  constexpr ExtendedStringView(const char (&str)[N]) : ftl::StringView(str) {}

  operator leveldb::Slice() const { return leveldb::Slice(data(), size()); }

  mojo::Array<uint8_t> ToArray();
};

// Returns the ExtendedStringView representation of the given value.
inline ExtendedStringView ToStringView(ExtendedStringView value) {
  return value;
}

// Returns the representation of the given value in LevelDB.
inline leveldb::Slice ToSlice(ExtendedStringView value) {
  return value;
}

// Returns the mojo::Array representation of the given value.
mojo::Array<uint8_t> ToArray(ExtendedStringView value);

// Returns the std::string representation of the given value.
std::string ToString(ExtendedStringView value);

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
