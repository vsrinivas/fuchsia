// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/strings/utf_codecs.h"

#include "lib/ftl/third_party/icu/icu_utf.h"

namespace ftl {

// ReadUnicodeCharacter --------------------------------------------------------

bool ReadUnicodeCharacter(const char* src,
                          int32_t src_len,
                          int32_t* char_index,
                          uint32_t* code_point_out) {
  // U8_NEXT expects to be able to use -1 to signal an error, so we must
  // use a signed type for code_point.  But this function returns false
  // on error anyway, so code_point_out is unsigned.
  int32_t code_point;
  FTL_U8_NEXT(src, *char_index, src_len, code_point);
  *code_point_out = static_cast<uint32_t>(code_point);

  // The ICU macro above moves to the next char, we want to point to the last
  // char consumed.
  (*char_index)--;

  // Validate the decoded value.
  return IsValidCodepoint(code_point);
}

// WriteUnicodeCharacter -------------------------------------------------------

size_t WriteUnicodeCharacter(uint32_t code_point, std::string* output) {
  if (code_point <= 0x7f) {
    // Fast path the common case of one byte.
    output->push_back(static_cast<char>(code_point));
    return 1;
  }

  // FTL_U8_APPEND_UNSAFE can append up to 4 bytes.
  size_t char_offset = output->length();
  size_t original_char_offset = char_offset;
  output->resize(char_offset + FTL_U8_MAX_LENGTH);

  FTL_U8_APPEND_UNSAFE(&(*output)[0], char_offset, code_point);

  // FTL_U8_APPEND_UNSAFE will advance our pointer past the inserted character,
  // so it will represent the new length of the string.
  output->resize(char_offset);
  return char_offset - original_char_offset;
}

}  // namespace ftl
