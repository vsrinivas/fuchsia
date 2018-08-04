// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_URL_URL_FILE_H_
#define LIB_URL_URL_FILE_H_

// Provides shared functions used by the internals of the parser and
// canonicalizer for file URLs. Do not use outside of these modules.

#include "lib/url/url_parse_internal.h"

namespace url {

// Returns the index of the next slash in the input after the given index, or
// spec_len if the end of the input is reached.
inline int FindNextSlash(const char* spec, size_t begin_index,
                         size_t spec_len) {
  size_t idx = begin_index;
  while (idx < spec_len && !IsURLSlash(spec[idx]))
    idx++;
  return idx;
}

}  // namespace url

#endif  // LIB_URL_URL_FILE_H_
