// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// The maximum recursion depth the fidl encoder or decoder will
// perform. Each nested aggregate type (structs, unions, arrays, or
// vectors) counts as one step in the recursion depth.
#define FIDL_RECURSION_DEPTH 32

// See https://fuchsia.googlesource.com/docs/+/master/development/languages/fidl/c.md#fidl_encode-fidl_encode_msg
zx_status_t fidl_encode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        zx_handle_t* handles, uint32_t max_handles,
                        uint32_t* out_actual_handles, const char** out_error_msg);
zx_status_t fidl_encode_msg(const fidl_type_t* type, fidl_msg_t* msg,
                            uint32_t* out_actual_handles, const char** out_error_msg);

// See https://fuchsia.googlesource.com/docs/+/master/development/languages/fidl/c.md#fidl_decode-fidl_decode_msg
zx_status_t fidl_decode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        const zx_handle_t* handles, uint32_t num_handles,
                        const char** error_msg_out);
zx_status_t fidl_decode_msg(const fidl_type_t* type, fidl_msg_t* msg,
                            const char** out_error_msg);

// Validates an encoded message against the given |type|.
//
// The |bytes| are not modified.
zx_status_t fidl_validate(const fidl_type_t* type, const void* bytes, uint32_t num_bytes,
                          uint32_t num_handles, const char** error_msg_out);
zx_status_t fidl_validate_msg(const fidl_type_t* type, const fidl_msg_t* msg,
                              const char** out_error_msg);

// Stores the name of a fidl type into the provided buffer.
// Truncates the name if it is too long to fit into the buffer.
// Returns the number of characters written into the buffer.
//
// Note: This function does not write a trailing NUL.
size_t fidl_format_type_name(const fidl_type_t* type,
                             char* buffer, size_t capacity);

// Sends an epitaph with the given values down the channel.
// TODO: Add docs on epitaphs to FIDL C bindings docs, point to it here.
zx_status_t fidl_epitaph_write(zx_handle_t channel, zx_status_t sys_error,
                               int32_t app_error);

__END_CDECLS
