// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls/object.h>

#include "drivers/audio/intel-hda/utils/utils.h"

namespace audio {
namespace intel_hda {

mx_obj_type_t GetHandleType(const mx::handle& handle) {
    mx_info_handle_basic_t basic_info;

    if (!handle.is_valid())
        return MX_OBJ_TYPE_NONE;

    mx_status_t res = handle.get_info(MX_INFO_HANDLE_BASIC,
                                      &basic_info, sizeof(basic_info),
                                      nullptr, nullptr);

    return (res == NO_ERROR) ? static_cast<mx_obj_type_t>(basic_info.type) : MX_OBJ_TYPE_NONE;
}

}  // namespace intel_hda
}  // namespace audio
