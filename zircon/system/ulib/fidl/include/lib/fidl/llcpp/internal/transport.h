// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_H_
#define LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_H_

#include <zircon/assert.h>
#include <zircon/fidl.h>

#include <cstdint>
#include <type_traits>

namespace fidl {

namespace internal {

// Generalized handle.
// Designed to avoid accidental casting to/from integer handle types.
class Handle {
 public:
  explicit constexpr Handle(uint32_t value) : value_(value) {}
  Handle(const Handle&) = default;
  Handle& operator=(const Handle&) = default;
  Handle(Handle&&) noexcept = default;
  Handle& operator=(Handle&&) noexcept = default;

  const uint32_t& value() const { return value_; }
  uint32_t& value() { return value_; }

 private:
  uint32_t value_;
};
constexpr Handle kInvalidHandle = Handle(0);

// Attributes of a handle, as defined in FIDL files.
// Intended to be extensible, for instance if a transport introduces a new object type.
struct HandleAttributes {
  zx_obj_type_t obj_type;
  zx_rights_t rights;
};

// Options controlling FIDL encode and decode.
// These are fixed and specified on the transport-level.
struct EncodingConfiguration {
  // Indicates if this transport supports iovec on write-path.
  // Iovec will always be used as the output format if it is supported.
  bool encode_supports_iovec;
  // Indicates if this transport supports iovec on read-path.
  // Iovec will always be used as the input format if it is supported.
  bool decode_supports_iovec;

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
  zx_status_t (*decode_process_handle)(Handle* handle, HandleAttributes attr,
                                       uint32_t metadata_index, const void* metadata_array,
                                       const char** error);
};

// Flags resulting from FIDL encode and used to control transport write.
// These are specified on a per-message basis.
struct EncodeFlags {};

// Flags resulting from transport read and used to control FIDL decode.
// These are specified on a per-message basis.
struct DecodeFlags {};

struct CallMethodArgs {
  const void* wr_data;
  const Handle* wr_handles;
  const void* wr_handle_metadata;
  uint32_t wr_data_count;
  uint32_t wr_handles_count;

  void* rd_data;
  Handle* rd_handles;
  void* rd_handle_metadata;
  uint32_t rd_data_capacity;
  uint32_t rd_handles_capacity;
};

// An instance of TransportVTable contains function definitions to implement transport-specific
// functionality.
struct TransportVTable {
  fidl_transport_type type;
  const EncodingConfiguration* encoding_configuration;

  // Write to the transport.
  // |handle_metadata| contains transport-specific metadata produced by
  // EncodingConfiguration::decode_process_handle.
  zx_status_t (*write)(Handle handle, EncodeFlags encode_flags, const void* data,
                       uint32_t data_count, const Handle* handles, const void* handle_metadata,
                       uint32_t handles_count);

  // Read from the transport.
  // This populates |handle_metadata|, which contains transport-specific metadata and will be
  // passed to EncodingConfiguration::decode_process_handle.
  zx_status_t (*read)(Handle handle, void* data, uint32_t data_capacity, Handle* handles,
                      void* handle_metadata, uint32_t handles_capacity,
                      DecodeFlags* out_decode_flags, uint32_t* out_data_actual_count,
                      uint32_t* out_handles_actual_count);

  // Perform a call on the transport.
  // The arguments are formatted in |cargs|, with the write direction args corresponding to
  // those in |write| and the read direction args corresponding to those in |read|.
  zx_status_t (*call)(Handle handle, EncodeFlags encode_flags, zx_time_t deadline,
                      CallMethodArgs cargs, DecodeFlags* out_decode_flags,
                      uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count);

  // Close the handle.
  void (*close)(Handle);
};

// A type-erased unowned transport (e.g. generalized zx::unowned_channel).
// Create an |AnyUnownedTransport| object with |MakeAnyUnownedTransport|, implemented for each of
// the transport types.
class AnyUnownedTransport {
 public:
  template <typename Transport>
  static constexpr AnyUnownedTransport Make(Handle handle) noexcept {
    return AnyUnownedTransport(&Transport::VTable, handle);
  }

  AnyUnownedTransport(const AnyUnownedTransport&) = default;
  AnyUnownedTransport& operator=(const AnyUnownedTransport&) = default;
  AnyUnownedTransport(AnyUnownedTransport&& other) noexcept = default;
  AnyUnownedTransport& operator=(AnyUnownedTransport&& other) noexcept = default;

  template <typename Transport>
  typename Transport::UnownedType get() const {
    ZX_ASSERT(vtable_->type == Transport::VTable.type);
    return typename Transport::UnownedType(handle_.value());
  }

  const EncodingConfiguration* get_encoding_configuration() {
    return vtable_->encoding_configuration;
  }

  zx_status_t write(EncodeFlags encode_flags, const void* data, uint32_t data_count,
                    const Handle* handles, const void* handle_metadata, uint32_t handles_count) {
    return vtable_->write(handle_, encode_flags, data, data_count, handles, handle_metadata,
                          handles_count);
  }

  zx_status_t read(void* data, uint32_t data_capacity, Handle* handles, void* handle_metadata,
                   uint32_t handles_capacity, DecodeFlags* out_decode_flags,
                   uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) {
    return vtable_->read(handle_, data, data_capacity, handles, handle_metadata, handles_capacity,
                         out_decode_flags, out_data_actual_count, out_handles_actual_count);
  }

  zx_status_t call(EncodeFlags encode_flags, zx_time_t deadline, CallMethodArgs cargs,
                   DecodeFlags* out_decode_flags, uint32_t* out_data_actual_count,
                   uint32_t* out_handles_actual_count) {
    return vtable_->call(handle_, encode_flags, deadline, cargs, out_decode_flags,
                         out_data_actual_count, out_handles_actual_count);
  }

 private:
  friend class AnyTransport;
  explicit constexpr AnyUnownedTransport(const TransportVTable* vtable, Handle handle)
      : vtable_(vtable), handle_(handle) {}

  [[maybe_unused]] const TransportVTable* vtable_;
  [[maybe_unused]] Handle handle_;
};

// A type-erased owned transport (e.g. generalized zx::channel).
// Create an |AnyTransport| object with |MakeAnyTransport|, implemented for each of
// the transport types.
class AnyTransport {
 public:
  template <typename Transport>
  static AnyTransport Make(Handle handle) noexcept {
    return AnyTransport(&Transport::VTable, handle);
  }

  AnyTransport(const AnyTransport&) = delete;
  AnyTransport& operator=(const AnyTransport&) = delete;

  AnyTransport(AnyTransport&& other) noexcept : vtable_(other.vtable_), handle_(other.handle_) {
    other.handle_ = kInvalidHandle;
  }
  AnyTransport& operator=(AnyTransport&& other) noexcept {
    vtable_ = other.vtable_;
    handle_ = other.handle_;
    other.handle_ = kInvalidHandle;
    return *this;
  }
  ~AnyTransport() {
    if (handle_.value() != kInvalidHandle.value()) {
      vtable_->close(handle_);
    }
  }

  constexpr AnyUnownedTransport borrow() const { return AnyUnownedTransport(vtable_, handle_); }

  template <typename Transport>
  typename Transport::UnownedType get() const {
    ZX_ASSERT(vtable_->type == Transport::VTable.type);
    return typename Transport::UnownedType(handle_.value());
  }

  template <typename Transport>
  typename Transport::OwnedType release() {
    ZX_ASSERT(vtable_->type == Transport::VTable.type);
    Handle temp = handle_;
    handle_ = kInvalidHandle;
    return typename Transport::OwnedType(temp.value());
  }

  const EncodingConfiguration* get_encoding_configuration() {
    return vtable_->encoding_configuration;
  }

  zx_status_t write(EncodeFlags encode_flags, const void* data, uint32_t data_count,
                    const Handle* handles, const void* handle_metadata, uint32_t handles_count) {
    return vtable_->write(handle_, encode_flags, data, data_count, handles, handle_metadata,
                          handles_count);
  }

  zx_status_t read(void* data, uint32_t data_capacity, Handle* handles, void* handle_metadata,
                   uint32_t handles_capacity, DecodeFlags* out_decode_flags,
                   uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) {
    return vtable_->read(handle_, data, data_capacity, handles, handle_metadata, handles_capacity,
                         out_decode_flags, out_data_actual_count, out_handles_actual_count);
  }

  zx_status_t call(EncodeFlags encode_flags, zx_time_t deadline, CallMethodArgs cargs,
                   DecodeFlags* out_decode_flags, uint32_t* out_data_actual_count,
                   uint32_t* out_handles_actual_count) {
    return vtable_->call(handle_, encode_flags, deadline, cargs, out_decode_flags,
                         out_data_actual_count, out_handles_actual_count);
  }

 private:
  explicit constexpr AnyTransport(const TransportVTable* vtable, Handle handle)
      : vtable_(vtable), handle_(handle) {}

  const TransportVTable* vtable_;
  Handle handle_;
};

AnyUnownedTransport MakeAnyUnownedTransport(const AnyTransport& transport);

template <typename TransportObject>
struct AssociatedTransportImpl;

template <typename TransportObject>
using AssociatedTransport = typename AssociatedTransportImpl<std::decay_t<TransportObject>>::type;

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_H_
