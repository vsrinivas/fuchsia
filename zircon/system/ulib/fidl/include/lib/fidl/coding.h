// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CODING_H_
#define LIB_FIDL_CODING_H_

#include <zircon/compiler.h>
#include <zircon/fidl.h>
#include <zircon/types.h>

__BEGIN_CDECLS

// The maximum recursion depth the fidl encoder or decoder will
// perform. Each nested aggregate type (structs, unions, arrays,
// vectors, or tables) counts as one step in the recursion depth.
#define FIDL_RECURSION_DEPTH 32

// See
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/languages/c.md#fidl_encode-fidl_encode_msg
zx_status_t fidl_encode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        zx_handle_t* handles, uint32_t max_handles, uint32_t* out_actual_handles,
                        const char** out_error_msg);
zx_status_t fidl_encode_etc(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                            zx_handle_disposition_t* handle_dispositions,
                            uint32_t max_handle_dispositions,
                            uint32_t* out_actual_handle_dispositions, const char** out_error_msg);
zx_status_t fidl_encode_msg(const fidl_type_t* type, fidl_outgoing_msg_t* msg,
                            uint32_t* out_actual_handles, const char** out_error_msg);

// fidl_linearize_and_encode converts an object and its children to a FIDL wire-format
// compatible byte array.
//
// As part of this,
// - Objects are "linearized", meaning they are laid out as a contiguous block in FIDL-wire format
// order. The object is walked by following pointers in depth-first order and each encountered
// block is copied in order encountered.
// - Pointers in the output buffer are replaced with FIDL_ALLOC_PRESENT or FIDL_ALLOC_ABSENT
// markers.
// - Handles in the output buffer are replaced with FIDL_HANDLE_PRESENT or FIDL_HANDLE_ABSENT
// markers.
// - Input handles are consumed and the values in the input object are replaced with
// ZX_HANDLE_INVALID.
// - Padding is zeroed in between struct fields and in between out of line values.
// - The buffer is validated to match the requirements of the FIDL wire format.
//
// On success, handles in the original object will be moved to the |out_handles| array.
// On failure, handles in the original object will be closed.
zx_status_t fidl_linearize_and_encode(const fidl_type_t* type, void* value, uint8_t* out_bytes,
                                      uint32_t num_bytes, zx_handle_t* out_handles,
                                      uint32_t num_handles, uint32_t* out_num_actual_bytes,
                                      uint32_t* out_num_actual_handles, const char** out_error_msg);
zx_status_t fidl_linearize_and_encode_etc(const fidl_type_t* type, void* value, uint8_t* out_bytes,
                                          uint32_t num_bytes, zx_handle_disposition_t* out_handles,
                                          uint32_t num_handles, uint32_t* out_num_actual_bytes,
                                          uint32_t* out_num_actual_handles,
                                          const char** out_error_msg);
zx_status_t fidl_linearize_and_encode_msg(const fidl_type_t* type, void* value,
                                          fidl_outgoing_msg_t* msg, uint32_t* out_num_actual_bytes,
                                          uint32_t* out_num_actual_handles,
                                          const char** out_error_msg);

// See
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/languages/c.md#fidl_decode-fidl_decode_msg
zx_status_t fidl_decode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        const zx_handle_t* handles, uint32_t num_handles,
                        const char** error_msg_out);
// Perform a fidl_decode, but leave unknown handles in flexible resource union types intact
// instead of closing them.
zx_status_t fidl_decode_skip_unknown_union_handles(const fidl_type_t* type, void* bytes,
                                                   uint32_t num_bytes, const zx_handle_t* handles,
                                                   uint32_t num_handles,
                                                   const char** error_msg_out);
zx_status_t fidl_decode_etc(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                            const zx_handle_info_t* handle_infos, uint32_t num_handle_infos,
                            const char** error_msg_out);
zx_status_t fidl_decode_msg(const fidl_type_t* type, fidl_incoming_msg_t* msg,
                            const char** out_error_msg);

// Validates an encoded message against the given |type|.
//
// The |bytes| are not modified.
zx_status_t fidl_validate(const fidl_type_t* type, const void* bytes, uint32_t num_bytes,
                          uint32_t num_handles, const char** out_error_msg);
zx_status_t fidl_validate_msg(const fidl_type_t* type, const fidl_outgoing_msg_t* msg,
                              const char** out_error_msg);

// Validates a FIDL string, and verifies that it is a well-formed UTF-8 code
// unit sequence. That is respect the UTF-8 encoding, and be formed solely of
// unicode scalar value, i.e. any Unicode code point except high-surrogate
// and low-surrogate code points.
//
// The |data| is not modified.
//
// See also http://www.unicode.org/versions/Unicode13.0.0/ch03.pdf#G7404
zx_status_t fidl_validate_string(const char* data, uint64_t size);

// Stores the name of a fidl type into the provided buffer.
// Truncates the name if it is too long to fit into the buffer.
// Returns the number of characters written into the buffer.
//
// Note: This function does not write a trailing NUL.
size_t fidl_format_type_name(const fidl_type_t* type, char* buffer, size_t capacity);

// The following functions are only available under Fuchsia.

#ifdef __Fuchsia__

// Traverses a decoded FIDL message starting at |value|, closing all handles within it.
// If the message is non-contiguous in memory, the function will follow pointers and close handles
// in any scattered out-of-line objects.
//
// Handle values in |value| are replaced with ZX_HANDLE_INVALID.
zx_status_t fidl_close_handles(const fidl_type_t* type, void* value, const char** out_error_msg);

#endif

__END_CDECLS

#endif  // LIB_FIDL_CODING_H_
