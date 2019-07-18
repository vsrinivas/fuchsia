// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_LIB_TYPE_DECODER_H_
#define TOOLS_FIDLCAT_LIB_TYPE_DECODER_H_

#include <zircon/system/public/zircon/rights.h>
#include <zircon/system/public/zircon/syscalls/object.h>
#include <zircon/system/public/zircon/types.h>

#include <cstdint>
#include <ostream>

namespace fidlcat {

struct Colors {
  Colors(const char* new_reset, const char* new_red, const char* new_green, const char* new_blue,
         const char* new_white_on_magenta)
      : reset(new_reset),
        red(new_red),
        green(new_green),
        blue(new_blue),
        white_on_magenta(new_white_on_magenta) {}

  const char* const reset;
  const char* const red;
  const char* const green;
  const char* const blue;
  const char* const white_on_magenta;
};

void ErrorName(int64_t error_code, std::ostream& os);
void ObjTypeName(zx_obj_type_t obj_type, std::ostream& os);
void RightsName(zx_rights_t rights, std::ostream& os);
void DisplayHandle(const Colors& colors, const zx_handle_info_t& handle, std::ostream& os);

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_LIB_TYPE_DECODER_H_
