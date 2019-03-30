// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <float.h>

#include <limits>

#include "garnet/bin/zxdb/console/string_formatters.h"
#include "src/developer/debug/zxdb/client/register.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

Err GetLittleEndianHexOutput(const std::vector<uint8_t>& value,
                             std::string* out, int length) {
  if (value.empty())
    return Err("Invalid size for hex printing: 0");

  if (length == 0)
    length = std::numeric_limits<int>::max();

  // For now we print into chunks of 32-bits, shortening the ends.
  auto cur = value.data();
  auto end = cur + value.size();
  std::vector<std::string> chunks;
  int added_bytes = 0;
  while (cur < end && added_bytes < length) {
    uint32_t val = 0;
    auto diff = end - cur;
    for (int i = 0; i < 4 && i < diff; i++) {
      uint32_t tmp = cur[i] << (i * 8);
      uint32_t mask = 0xFF << (i * 8);
      val = (val & ~mask) | tmp;
    }

    chunks.push_back(fxl::StringPrintf("%.8x", val));
    cur += 4;
    added_bytes += 4;
  }

  // Though each particular chunk correctly keeps the endianness, the order is
  // backwards, as the last bits of the register would be printed first.
  // For that we append the chunks backwards.
  out->clear();
  auto cit = chunks.rbegin();
  while (cit != chunks.rend()) {
    out->append(*cit);
    cit++;
    if (cit != chunks.rend())
      out->append(" ");
  }
  return Err();
}

Err GetFPString(const std::vector<uint8_t>& value, std::string* out,
                int precision) {
  switch (value.size()) {
    case 4:
      precision = precision != 0 ? precision : FLT_DIG;
      *out =
          fxl::StringPrintf("%.*e", precision, *(const float*)(value.data()));
      return Err();
    case 8:
      precision = precision != 0 ? precision : DBL_DIG;
      *out =
          fxl::StringPrintf("%.*e", precision, *(const double*)(value.data()));
      return Err();
    case 16:
      precision = precision != 0 ? precision : LDBL_DIG;
      *out = fxl::StringPrintf("%.*Le", precision,
                               *(const long double*)(value.data()));
      return Err();
    default:
      return Err(fxl::StringPrintf(
          "Wrong size for floating point printing: %zu", value.size()));
  }
}

}  // namespace zxdb
