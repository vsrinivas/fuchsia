// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// C++ utilities for formatting sizes to make them more human-readable.

#ifndef PRETTY_CPP_SIZES_H_
#define PRETTY_CPP_SIZES_H_

#include <zircon/compiler.h>
#include <zircon/types.h>

#include <optional>
#include <string_view>

#include <pretty/sizes.h>

namespace pretty {

// Units for formatting byte sizes.
enum class SizeUnit : char {
  kAuto = 0,  // Automatically select an appropriate unit.
  kBytes = 'B',
  kKiB = 'K',
  kMiB = 'M',
  kGiB = 'G',
  kTiB = 'T',
  kPiB = 'P',
  kEiB = 'E',
};

// FormattedBytes is an inline buffer suitable for containing formatted byte
// sizes.
//
// Typical usage is as follows:
//
//   printf("Free memory: %s\n", FormattedBytes(12345).str());
//
// See `format_size` and `format_size_fixed` in <pretty/sizes.h> for details.
class FormattedBytes {
 public:
  // Returns how the given SizeUnit will be formatted, except in the case of
  // kAuto, for which the empty string is returned.
  static std::string_view ToString(SizeUnit unit);

  // Construct an empty string.
  FormattedBytes() { buff_[0] = 0; }

  // Construct a string representing the given size.
  //
  // Chooses an appropriate unit ('k', 'M', etc) based on the size.
  explicit FormattedBytes(size_t size) { SetSize(size); }

  // Construct a string representing the given size, using the given units.
  FormattedBytes(size_t size, SizeUnit unit) { SetSize(size, unit); }

  // Default copy operators.
  FormattedBytes(const FormattedBytes&) = default;
  FormattedBytes& operator=(const FormattedBytes&) = default;

  // Update the string to the given size.
  FormattedBytes& SetSize(size_t size) {
    format_size(buff_, sizeof(buff_), size);
    return *this;
  }
  FormattedBytes& SetSize(size_t size, SizeUnit unit) {
    format_size_fixed(buff_, sizeof(buff_), size, static_cast<char>(unit));
    return *this;
  }

  // Return the formatted string.
  std::string_view str() const { return buff_; }

  // Return the formatted string as a C-style NUL-terminated string.
  const char* c_str() const { return buff_; }

  // Returns the formatted magnitude as a string.
  std::string_view Magnitude() const {
    auto sv = str();
    sv.remove_suffix(std::min<size_t>(1, sv.size()));
    return sv;
  }

  // Returns the associated SizeUnit. In the case of the empty string, kAuto
  // is returned.
  SizeUnit Unit() const {
    auto sv = str();
    return sv.empty() ? SizeUnit::kAuto : static_cast<SizeUnit>(sv.back());
  }

 private:
  // The formatted string.
  char buff_[MAX_FORMAT_SIZE_LEN];
};

// Returns the number of bytes represented by a human readable string
// like "123.4k", 123.4 * 1024 bytes encoded in |formatted_bytes|.
//
// If |formatted_bytes| is not correctly formatted then |std::nullopt| is returned.
//
// This is a reverse function of |format_size| input |bytes|. Except that it considers
// absence of unit (e.g. "123") to be in bytes(implicit B).
std::optional<uint64_t> ParseSizeBytes(std::string_view formatted_bytes);

}  // namespace pretty

#endif  // PRETTY_CPP_SIZES_H_
