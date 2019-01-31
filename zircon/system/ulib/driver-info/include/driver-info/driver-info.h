// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/driver/binding.h>

__BEGIN_CDECLS;

typedef void (*di_info_func_t)(zircon_driver_note_payload_t* note,
                               const zx_bind_inst_t* binding, void *cookie);

zx_status_t di_read_driver_info(int fd, void* cookie, di_info_func_t func);

typedef zx_status_t (*di_read_func_t)(void* obj, void* data, size_t len, size_t off);

zx_status_t di_read_driver_info_etc(void* obj, di_read_func_t rfunc,
                                    void* cookie, di_info_func_t ifunc);

// Lookup the human readable name of a bind program parameter, or return NULL if
// the name is not known.  Used by debug code to do things like dump the
// published parameters of a device, or dump the bind program of a driver.
const char* di_bind_param_name(uint32_t param_num);

// Disassemble a bind program instruction and dump it to the buffer provided by
// the caller.  If the buffer is too small, the disassembly may be truncated.
void di_dump_bind_inst(const zx_bind_inst_t* b, char* buf, size_t buf_len);

__END_CDECLS;
