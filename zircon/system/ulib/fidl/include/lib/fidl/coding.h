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
zx_status_t fidl_encode_msg(const fidl_type_t* type, fidl_msg_t* msg, uint32_t* out_actual_handles,
                            const char** out_error_msg);

// See
// https://fuchsia.dev/fuchsia-src/development/languages/fidl/languages/c.md#fidl_decode-fidl_decode_msg
zx_status_t fidl_decode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        const zx_handle_t* handles, uint32_t num_handles,
                        const char** error_msg_out);
zx_status_t fidl_decode_msg(const fidl_type_t* type, fidl_msg_t* msg, const char** out_error_msg);

// Validates an encoded message against the given |type|.
//
// The |bytes| are not modified.
zx_status_t fidl_validate(const fidl_type_t* type, const void* bytes, uint32_t num_bytes,
                          uint32_t num_handles, const char** out_error_msg);
zx_status_t fidl_validate_msg(const fidl_type_t* type, const fidl_msg_t* msg,
                              const char** out_error_msg);

// Follow an object tree and copy the elements into the provided buffer, such that the
// resulting buffer is ready for fidl_encode.
//
// Starting from the root of the objects specified in |value|,
// This function assumes that |buffer| points to an uninitialized memory region
// of size at least |num_bytes|, large enough to hold the entire encoded FIDL message.
// It will follow pointers and pack secondary objects after the primary
// as per the wire-format, in their correct order and ensuring alignment.
// The resulting message length within the buffer is returned via |out_num_bytes|.
//
// Upon success, the handles in the object tree will be moved to the buffer;
// the remaining contents in the original object tree are otherwise untouched.
// In case of any failure, the handles in the original tree will stay intact.
//
// It will return ZX_ERR_BUFFER_TOO_SMALL if the provided buffer is not large enough
// to hold the entire message.
zx_status_t fidl_linearize(const fidl_type_t* type, void* value, uint8_t* buffer,
                           uint32_t num_bytes, uint32_t* out_num_bytes, const char** out_error_msg);

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
zx_status_t fidl_close_handles_msg(const fidl_type_t* type, const fidl_msg_t* msg,
                                   const char** out_error_msg);

#endif

__END_CDECLS

#endif  // LIB_FIDL_CODING_H_
