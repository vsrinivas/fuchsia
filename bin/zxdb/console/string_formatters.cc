// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <float.h>

#include "garnet/bin/zxdb/client/register.h"
#include "garnet/bin/zxdb/console/string_formatters.h"
#include "garnet/public/lib/fxl/strings/string_printf.h"

namespace zxdb {

Err GetFPString(const Register& reg, std::string* out, int precision) {
  switch (reg.size()) {
    case 4:
      precision = precision != 0 ? precision : FLT_DIG;
      *out = fxl::StringPrintf("%.*e", precision,
                               *(const float*)(&reg.data().front()));
      return Err();
    case 8:
      precision = precision != 0 ? precision : DBL_DIG;
      *out = fxl::StringPrintf("%.*e", precision,
                               *(const double*)(&reg.data().front()));
      return Err();
    case 16:
      precision = precision != 0 ? precision : LDBL_DIG;
      *out = fxl::StringPrintf("%.*Le", precision,
                               *(const long double*)(&reg.data().front()));
      return Err();
    default:
      return Err(fxl::StringPrintf(
          "Wrong size for floating point printing: %zu", reg.size()));
  }
}

}  // namespace zxdb
