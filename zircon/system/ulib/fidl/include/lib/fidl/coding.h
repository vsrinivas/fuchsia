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
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/tutorials/tutorial-c#fidl_encode-fidl_encode_msg
zx_status_t fidl_encode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        zx_handle_t* handles, uint32_t max_handles, uint32_t* out_actual_handles,
                        const char** out_error_msg);
zx_status_t fidl_encode_etc(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                            zx_handle_disposition_t* handle_dispositions,
                            uint32_t max_handle_dispositions,
                            uint32_t* out_actual_handle_dispositions, const char** out_error_msg);
zx_status_t fidl_encode_msg(const fidl_type_t* type, fidl_outgoing_msg_byte_t* msg,
                            uint32_t* out_actual_handles, const char** out_error_msg);

// Perform a fidl_decode, but leave unknown handles in flexible resource types intact
// instead of closing them.
zx_status_t fidl_decode_skip_unknown_handles(const fidl_type_t* type, void* bytes,
                                             uint32_t num_bytes, const zx_handle_t* handles,
                                             uint32_t num_handles, const char** error_msg_out);
zx_status_t internal__fidl_decode_skip_unknown_handles__v2__may_break(
    const fidl_type_t* type, void* bytes, uint32_t num_bytes, const zx_handle_t* handles,
    uint32_t num_handles, const char** error_msg_out);

// Perform a fidl_decode and check handle types and rights against the types and rights specified
// in the FIDL file.
//
// It is an error for a zx_handle_info_t to contain a handle type that does not match what is
// expected from FIDL unless either the expected or actual type is ZX_OBJ_TYPE_NONE.
// It is also error if there are fewer actual rights than expected rights and the actual or
// expected rights are not ZX_RIGHT_SAME_RIGHTS. If there are more actual rights than expected
// rights, the actual rights will be reduced to the expected rights via a call to
// zx_handle_replace.
zx_status_t fidl_decode_etc(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                            const zx_handle_info_t* handle_infos, uint32_t num_handle_infos,
                            const char** error_msg_out);
zx_status_t internal_fidl_decode_etc__v2__may_break(const fidl_type_t* type, void* bytes,
                                                    uint32_t num_bytes,
                                                    const zx_handle_info_t* handle_infos,
                                                    uint32_t num_handle_infos,
                                                    const char** error_msg_out);

// Perform a fidl_decode_etc, but leave unknown handles in flexible resource types intact
// instead of closing them.
zx_status_t fidl_decode_etc_skip_unknown_handles(const fidl_type_t* type, void* bytes,
                                                 uint32_t num_bytes,
                                                 const zx_handle_info_t* handles,
                                                 uint32_t num_handles, const char** error_msg_out);
zx_status_t internal__fidl_decode_etc_skip_unknown_handles__v2__may_break(
    const fidl_type_t* type, void* bytes, uint32_t num_bytes, const zx_handle_info_t* handles,
    uint32_t num_handles, const char** error_msg_out);
zx_status_t fidl_decode_msg(const fidl_type_t* type, fidl_incoming_msg_t* msg,
                            const char** out_error_msg);

// Validates an encoded message against the given |type|.
//
// The |bytes| are not modified.
zx_status_t fidl_validate(const fidl_type_t* type, const void* bytes, uint32_t num_bytes,
                          uint32_t num_handles, const char** out_error_msg);
zx_status_t fidl_validate_msg(const fidl_type_t* type, const fidl_outgoing_msg_byte_t* msg,
                              const char** out_error_msg);

// Validates an encoded message against the given |type|.
//
// The |bytes| are not modified.
//
// This is a version of the FIDL validator that validates against the v2 wire format.
// IT MAY BREAK AT ANY TIME OR BE REMOVED WITHOUT NOTICE.
zx_status_t internal__fidl_validate__v2__may_break(const fidl_type_t* type, const void* bytes,
                                                   uint32_t num_bytes, uint32_t num_handles,
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
