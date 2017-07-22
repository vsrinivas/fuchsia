// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/compiler.h>
#include <magenta/types.h>
#include <magenta/driver/binding.h>

__BEGIN_CDECLS;

mx_status_t di_read_driver_info(int fd, void *cookie,
                                void (*func)(
                                    magenta_driver_note_payload_t* note,
                                    const mx_bind_inst_t* binding,
                                    void *cookie));

// Lookup the human readable name of a bind program parameter, or return NULL if
// the name is not known.  Used by debug code to do things like dump the
// published parameters of a device, or dump the bind program of a driver.
const char* di_bind_param_name(uint32_t param_num);

// Disassemble a bind program instruction and dump it to the buffer provided by
// the caller.  If the buffer is too small, the disassembly may be truncated.
void di_dump_bind_inst(const mx_bind_inst_t* b, char* buf, size_t buf_len);

__END_CDECLS;
