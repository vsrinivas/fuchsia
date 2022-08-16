// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_RIGHTS_H_
#define SRC_LIB_ZXDUMP_RIGHTS_H_

#include <zircon/types.h>

namespace zxdump {

constexpr zx_rights_t kProcessRights =  // Rights we need...
    ZX_RIGHT_INSPECT |                  // to do get_info;
    ZX_RIGHT_MANAGE_PROCESS |           // for some special info types,
    ZX_RIGHT_MANAGE_THREAD |            // these are needed too;
    ZX_RIGHT_ENUMERATE |                // to do get_child;
    ZX_RIGHTS_IO;                       // to do process_read_memory.

constexpr zx_rights_t kThreadRights =  // Rights we need...
    ZX_RIGHT_WAIT |                    // to wait for suspension;
    ZX_RIGHT_INSPECT |                 // to do get_info;
    ZX_RIGHT_GET_PROPERTY |            // to do get_property;
    ZX_RIGHT_READ;                     // to do thread_state_read.

// Note also that the rights needed on a process are needed on a job to get
// those rights on its processes, and the rights needed on a thread are
// needed on a process to get those rights on its threads.
constexpr zx_rights_t kChildRights = kProcessRights | kThreadRights;

}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_RIGHTS_H_
