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
//
// These methods expect non-transactional messages.
zx_status_t fidl_encode(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                        zx_handle_t* handles, uint32_t max_handles, uint32_t* out_actual_handles,
                        const char** out_error_msg);
zx_status_t fidl_encode_etc(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                            zx_handle_disposition_t* handle_dispositions,
                            uint32_t max_handle_dispositions,
                            uint32_t* out_actual_handle_dispositions, const char** out_error_msg);

// This method assumes that the message being encoded is transactional (ie, that it includes a
// leading 16-byte header).
//
// This method is only intended for use by the deprecated FIDL C bindings.
zx_status_t fidl_encode_msg(const fidl_type_t* type, fidl_outgoing_msg_byte_t* msg,
                            uint32_t* out_actual_handles, const char** out_error_msg);

// Perform a fidl_decode and check handle types and rights against the types and rights specified in
// the FIDL file.
//
// It is an error for a zx_handle_info_t to contain a handle type that does not match what is
// expected from FIDL unless either the expected or actual type is ZX_OBJ_TYPE_NONE. It is also
// error if there are fewer actual rights than expected rights and the actual or expected rights are
// not ZX_RIGHT_SAME_RIGHTS. If there are more actual rights than expected rights, the actual rights
// will be reduced to the expected rights via a call to zx_handle_replace.
//
// This method expects non-transactional messages.
zx_status_t fidl_decode_etc(const fidl_type_t* type, void* bytes, uint32_t num_bytes,
                            const zx_handle_info_t* handle_infos, uint32_t num_handle_infos,
                            const char** error_msg_out);
zx_status_t internal_fidl_decode_etc__v2__may_break(const fidl_type_t* type, void* bytes,
                                                    uint32_t num_bytes,
                                                    const zx_handle_info_t* handle_infos,
                                                    uint32_t num_handle_infos,
                                                    const char** error_msg_out);

// Perform a fidl_decode_etc as input for HLCPP (leave unknown handles in flexible resource types
// intact instead of closing them, add offsets to unknown envelopes).
// IT MAY BREAK AT ANY TIME OR BE REMOVED WITHOUT NOTICE.
zx_status_t internal__fidl_decode_etc_hlcpp__v2__may_break(const fidl_type_t* type, void* bytes,
                                                           uint32_t num_bytes,
                                                           const zx_handle_info_t* handles,
                                                           uint32_t num_handles,
                                                           const char** error_msg_out);

// This function assumes that the message being passed in has a 16-byte transaction header
// attached.
//
// This method is only intended for use by the deprecated FIDL C bindings.
zx_status_t fidl_decode_msg(const fidl_type_t* type, fidl_incoming_msg_t* msg,
                            const char** out_error_msg);

// Validates an encoded message against the given |type|.
//
// The |bytes| are not modified.
//
// This is a version of the FIDL validator that validates against the v1 wire format.
// IT MAY BREAK AT ANY TIME OR BE REMOVED WITHOUT NOTICE.
zx_status_t internal__fidl_validate__v1__may_break(const fidl_type_t* type, const void* bytes,
                                                   uint32_t num_bytes, uint32_t num_handles,
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

// Traverses a decoded FIDL message starting at |value|, closing all handles within it. If the
// message is non-contiguous in memory, the function will follow pointers and close handles in any
// scattered out-of-line objects.
//
// Handle values in |value| are replaced with ZX_HANDLE_INVALID.
//
// This method expects non-transactional messages. If callers want to call this function on
// transacational inputs, they must first perform |::fidl::internal::fidl_exclude_header_bytes| on
// the input |bytes| and |num_bytes| values to trim the header bytes.
zx_status_t fidl_close_handles(const fidl_type_t* type, void* value, const char** out_error_msg);

#endif

__END_CDECLS

#ifdef __cplusplus

namespace fidl {
namespace internal {

// Attributes of a handle, as defined in FIDL files.
// Intended to be extensible, for instance if a transport introduces a new object type then
// it will be included in addition to the initial fields.
struct HandleAttributes {
  zx_obj_type_t obj_type;
  zx_rights_t rights;
};

// Options controlling FIDL encode and decode.
// These are fixed and specified on the transport-level.
struct CodingConfig {
  // Max supported number of iovecs on the write path.
  // Used to limit the number produced by encode_process_handle.
  uint32_t max_iovecs_write;

  // Callback to process a single handle during encode.
  // |out_metadata_array| contains an array of transport-specific metadata being outputted.
  // |metadata_index| contains an index to a specific metadata item corresponding to the current
  // handle. The implementation should populate out_metadata_array[metadata_index].
  zx_status_t (*encode_process_handle)(HandleAttributes attr, uint32_t metadata_index,
                                       void* out_metadata_array, const char** out_error);

  // Callback to process a single handle during decode.
  // |metadata_array| contains an array of transport-specific metadata.
  // |metadata_index| contains an index to a specific metadata item corresponding to the current
  // handle.
  zx_status_t (*decode_process_handle)(fidl_handle_t* handle, HandleAttributes attr,
                                       uint32_t metadata_index, const void* metadata_array,
                                       const char** error);
};

}  // namespace internal
}  // namespace fidl

// Internal fidl decode implementation, exposed for the purpose of sharing an implementation with
// LLCPP decode.
template <FidlWireFormatVersion WireFormatVersion>
zx_status_t internal__fidl_decode_impl__may_break(
    const fidl::internal::CodingConfig& encoding_configuration, const fidl_type_t* type,
    void* bytes, uint32_t num_bytes, const fidl_handle_t* handles, const void* handle_metadata,
    uint32_t num_handles, const char** out_error_msg, bool hlcpp_mode);
#endif

#endif  // LIB_FIDL_CODING_H_
