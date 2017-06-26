// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <magenta/syscalls/object.h>
#include <magenta/types.h>

__BEGIN_CDECLS

// Reads the mx_info_vmo_t entries for the process.
// Caller is responsible for the |out_vmos| pointer.
mx_status_t get_vmos(mx_handle_t process,
                     mx_info_vmo_t** out_vmos, size_t* out_count,
                     size_t* out_avail);

__END_CDECLS
