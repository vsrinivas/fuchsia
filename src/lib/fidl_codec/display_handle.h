// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CODEC_DISPLAY_HANDLE_H_
#define SRC_LIB_FIDL_CODEC_DISPLAY_HANDLE_H_

#include <zircon/system/public/zircon/types.h>

#include <ostream>

#include "src/lib/fidl_codec/printer.h"
#include "zircon/system/public/zircon/syscalls/debug.h"

namespace fidl_codec {

void DisplayHandle(const fidl_codec::Colors& colors, const zx_handle_info_t& handle,
                   std::ostream& os);
void ObjTypeName(zx_obj_type_t obj_type, std::ostream& os);
void RightsName(zx_rights_t rights, std::ostream& os);

}  // namespace fidl_codec

#endif  // SRC_LIB_FIDL_CODEC_DISPLAY_HANDLE_H_
