// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <zircon/compiler.h>
#include <zircon/syscalls/object.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// Reads the zx_info_vmo_t entries for the process.
// Caller is responsible for the |out_vmos| pointer.
zx_status_t get_vmos(zx_handle_t process,
                     zx_info_vmo_t** out_vmos, size_t* out_count,
                     size_t* out_avail);

__END_CDECLS
