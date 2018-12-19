// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "walker.h"

namespace fidl {

zx_status_t GetPrimaryObjectSize(const fidl_type_t* type,
                                 size_t* out_size,
                                 const char** out_error) {
    switch (type->type_tag) {
        case fidl::kFidlTypeStruct:
            *out_size = type->coded_struct.size;
            return ZX_OK;
        case fidl::kFidlTypeTable:
            *out_size = sizeof(fidl_vector_t);
            return ZX_OK;
        default:
            if (out_error) {
                *out_error = "Message must be a struct or a table";
            }
            return ZX_ERR_INVALID_ARGS;
    }
}

}
