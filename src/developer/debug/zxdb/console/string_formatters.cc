// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/string_formatters.h"

#include <float.h>

#include <limits>

#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

std::string GetLittleEndianHexOutput(containers::array_view<uint8_t> data) {
  if (data.empty())
    return std::string();

  // For now we print into chunks of 32-bits, shortening the ends.
  auto cur = data.data();
  auto end = cur + data.size();
  std::vector<std::string> chunks;
  size_t added_bytes = 0;
  while (cur < end && added_bytes < data.size()) {
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
  std::string result;
  auto cit = chunks.rbegin();
  while (cit != chunks.rend()) {
    result.append(*cit);
    cit++;
    if (cit != chunks.rend())
      result.append(" ");
  }
  return result;
}

std::string GetFPString(containers::array_view<uint8_t> value, int precision) {
  switch (value.size()) {
    case 4:
      precision = precision != 0 ? precision : FLT_DIG;
      return fxl::StringPrintf("%.*e", precision, *(const float*)(value.data()));
    case 8:
      precision = precision != 0 ? precision : DBL_DIG;
      return fxl::StringPrintf("%.*e", precision, *(const double*)(value.data()));
    case 16:
      if (sizeof(long double) == sizeof(double)) {
        // On systems that don't have a longer "long double" type (ARM), printf won't format it
        // properly.
        return "Can't format a 'long double' on this system.";
      } else {
        precision = precision != 0 ? precision : LDBL_DIG;
        return fxl::StringPrintf("%.*Le", precision, *(const long double*)(value.data()));
      }
    default:
      return fxl::StringPrintf("Wrong size for floating point printing: %zu", value.size());
  }
}

}  // namespace zxdb
