// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <string.h>

template <typename T>
size_t WritePaddedInternal(T* buffer, const void* msg, size_t length) {
  size_t needs_padding = (length % sizeof(T)) > 0;
  size_t padding = sizeof(T) - (length % sizeof(T));
  // Multiply by needs_padding to set padding to 0 if no padding
  // is necessary. This avoids unnecessary branching.
  padding *= needs_padding;
  // If we added padding -- zero the padding bytes in a single write operation
  size_t is_nonzero_length = length != 0;
  size_t eof_in_bytes = length + padding;
  size_t eof_in_words = eof_in_bytes / sizeof(T);
  size_t last_word = eof_in_words - is_nonzero_length;
  // Set the last word in the buffer to zero before writing
  // the data to it if we added padding. If we didn't add padding,
  // multiply by 1 which ends up writing back the current contents of that word
  // resulting in a NOP.
  buffer[last_word] *= !needs_padding;
  memcpy(buffer, msg, length);
  return (length + padding) / sizeof(T);
}
