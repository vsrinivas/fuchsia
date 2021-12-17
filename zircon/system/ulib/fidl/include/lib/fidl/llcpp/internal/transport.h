// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_H_
#define LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_H_

#include <lib/fidl/coding.h>
#include <lib/fidl/llcpp/internal/any.h>
#include <lib/fidl/llcpp/result.h>
#include <lib/fit/function.h>
#include <zircon/assert.h>
#include <zircon/fidl.h>

#include <cstdint>
#include <type_traits>

#ifdef __Fuchsia__
#include <lib/async/dispatcher.h>
#endif

namespace fidl {
namespace internal {
// Placeholder type for transport specific context type in the outgoing direction.
// reinterpret_cast into the appropriate type for the transport.
struct OutgoingTransportContext;
// Placeholder type for transport specific context type in the incoming direction.
// reinterpret_cast into the appropriate type for the transport.
struct IncomingTransportContext;
}  // namespace internal

// Options passed from the user-facing write API to transport write().
struct WriteOptions {
  // Transport specific context.
  internal::OutgoingTransportContext* outgoing_transport_context;
};

// Options passed from the user-facing read API to transport read().
struct ReadOptions {
  bool discardable = false;
  // Transport specific context populated by read.
  internal::IncomingTransportContext** out_incoming_transport_context;
};

// Options passed from the user-facing call API to transport call().
struct CallOptions {
  zx_time_t deadline = ZX_TIME_INFINITE;

  // Transport specific context.
  internal::OutgoingTransportContext* outgoing_transport_context;
  // Transport specific context populated by call.
  internal::IncomingTransportContext** out_incoming_transport_context;
};

class IncomingMessage;

namespace internal {

struct CallMethodArgs {
  const void* wr_data;
  const fidl_handle_t* wr_handles;
  const void* wr_handle_metadata;
  uint32_t wr_data_count;
  uint32_t wr_handles_count;

  void* rd_data;
  fidl_handle_t* rd_handles;
  void* rd_handle_metadata;
  uint32_t rd_data_capacity;
  uint32_t rd_handles_capacity;
};

// Generic interface for waiting on a transport (for new messages, peer close, etc).
// This is created by |create_waiter| in |TransportVTable|.
struct TransportWaiter {
  // Begin waiting. Invokes the success or failure handler when the wait completes.
  //
  // Exactly one of the wait's handlers will be invoked exactly once per Begin() call
  // unless the wait is canceled. When the dispatcher is shutting down (being destroyed),
  // the handlers of all remaining waits will be invoked with a status of |ZX_ERR_CANCELED|.
  //
  // Returns |ZX_OK| if the wait was successfully begun.
  // Returns |ZX_ERR_ACCESS_DENIED| if the object does not have |ZX_RIGHT_WAIT|.
  // Returns |ZX_ERR_BAD_STATE| if the dispatcher is shutting down.
  // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
  //
  // This operation is thread-safe.
  virtual zx_status_t Begin() = 0;

  // Cancels any wait started on the waiter.
  //
  // If successful, the wait's handler will not run.
  //
  // Returns |ZX_OK| if the wait was pending and it has been successfully
  // canceled; its handler will not run again and can be released immediately.
  // Returns |ZX_ERR_NOT_FOUND| if there was no pending wait either because it
  // already completed, had not been started, or its completion packet has been
  // dequeued from the port and is pending delivery to its handler (perhaps on
  // another thread).
  // Returns |ZX_ERR_NOT_SUPPORTED| if not supported by the dispatcher.
  //
  // This operation is thread-safe.
  virtual zx_status_t Cancel() = 0;

  virtual ~TransportWaiter() = default;
};

// Storage for |TransportWaiter|.
// This avoids heap allocation while using a virtual waiter interface.
// |kCapacity| must be larger than the sizes of all of the individual transport waiters.
using AnyTransportWaiter = Any<TransportWaiter, /* kCapacity= */ 256ull>;

// Function receiving notification of successful waits on a TransportWaiter.
using TransportWaitSuccessHandler =
    fit::inline_function<void(fidl::IncomingMessage&, IncomingTransportContext* transport_context)>;

// Function receiving notification of failing waits on a TransportWaiter.
using TransportWaitFailureHandler = fit::inline_function<void(UnbindInfo)>;

// An instance of TransportVTable contains function definitions to implement transport-specific
// functionality.
struct TransportVTable {
  fidl_transport_type type;
  const CodingConfig* encoding_configuration;

  // Write to the transport.
  // |handle_metadata| contains transport-specific metadata produced by
  // EncodingConfiguration::decode_process_handle.
  zx_status_t (*write)(fidl_handle_t handle, const WriteOptions& options, const void* data,
                       uint32_t data_count, const fidl_handle_t* handles,
                       const void* handle_metadata, uint32_t handles_count);

  // Read from the transport.
  // This populates |handle_metadata|, which contains transport-specific metadata and will be
  // passed to EncodingConfiguration::decode_process_handle.
  zx_status_t (*read)(fidl_handle_t handle, const ReadOptions& options, void* data,
                      uint32_t data_capacity, fidl_handle_t* handles, void* handle_metadata,
                      uint32_t handles_capacity, uint32_t* out_data_actual_count,
                      uint32_t* out_handles_actual_count);

  // Perform a call on the transport.
  // The arguments are formatted in |cargs|, with the write direction args corresponding to
  // those in |write| and the read direction args corresponding to those in |read|.
  zx_status_t (*call)(fidl_handle_t handle, const CallOptions& options, const CallMethodArgs& cargs,
                      uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count);

#ifdef __Fuchsia__
  // Create a waiter object to wait for messages on the transport.
  // No waits are started initially on the waiter. Call Begin() to start waiting.
  // The waiter object is output into |any_transport_waiter|.
  zx_status_t (*create_waiter)(fidl_handle_t handle, async_dispatcher_t* dispatcher,
                               TransportWaitSuccessHandler success_handler,
                               TransportWaitFailureHandler failure_handler,
                               AnyTransportWaiter& any_transport_waiter);
#endif

  // Close the handle.
  void (*close)(fidl_handle_t);
};

// A type-erased unowned transport (e.g. generalized zx::unowned_channel).
// Create an |AnyUnownedTransport| object with |MakeAnyUnownedTransport|, implemented for each of
// the transport types.
class AnyUnownedTransport {
 public:
  template <typename Transport>
  static constexpr AnyUnownedTransport Make(fidl_handle_t handle) noexcept {
    return AnyUnownedTransport(&Transport::VTable, handle);
  }

  AnyUnownedTransport(const AnyUnownedTransport&) = default;
  AnyUnownedTransport& operator=(const AnyUnownedTransport&) = default;
  AnyUnownedTransport(AnyUnownedTransport&& other) noexcept = default;
  AnyUnownedTransport& operator=(AnyUnownedTransport&& other) noexcept = default;

  template <typename Transport>
  typename Transport::UnownedType get() const {
    ZX_ASSERT(vtable_->type == Transport::VTable.type);
    return typename Transport::UnownedType(handle_);
  }

  bool is_valid() const { return handle_ != FIDL_HANDLE_INVALID; }

  const TransportVTable* vtable() const { return vtable_; }

  fidl_handle_t handle() const { return handle_; }

  fidl_transport_type type() const { return vtable_->type; }

  zx_status_t write(const WriteOptions& options, const void* data, uint32_t data_count,
                    const fidl_handle_t* handles, const void* handle_metadata,
                    uint32_t handles_count) const {
    return vtable_->write(handle_, options, data, data_count, handles, handle_metadata,
                          handles_count);
  }

  zx_status_t read(const ReadOptions& options, void* data, uint32_t data_capacity,
                   fidl_handle_t* handles, void* handle_metadata, uint32_t handles_capacity,
                   uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) const {
    return vtable_->read(handle_, options, data, data_capacity, handles, handle_metadata,
                         handles_capacity, out_data_actual_count, out_handles_actual_count);
  }

  zx_status_t call(const CallOptions& options, const CallMethodArgs& cargs,
                   uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) const {
    return vtable_->call(handle_, options, cargs, out_data_actual_count, out_handles_actual_count);
  }

#ifdef __Fuchsia__
  zx_status_t create_waiter(async_dispatcher_t* dispatcher,
                            TransportWaitSuccessHandler success_handler,
                            TransportWaitFailureHandler failure_handler,
                            AnyTransportWaiter& any_transport_waiter) const {
    return vtable_->create_waiter(handle_, dispatcher, std::move(success_handler),
                                  std::move(failure_handler), any_transport_waiter);
  }
#endif

 private:
  friend class AnyTransport;
  explicit constexpr AnyUnownedTransport(const TransportVTable* vtable, fidl_handle_t handle)
      : vtable_(vtable), handle_(handle) {}

  [[maybe_unused]] const TransportVTable* vtable_;
  [[maybe_unused]] fidl_handle_t handle_;
};

// A type-erased owned transport (e.g. generalized zx::channel).
// Create an |AnyTransport| object with |MakeAnyTransport|, implemented for each of
// the transport types.
class AnyTransport {
 public:
  template <typename Transport>
  static AnyTransport Make(fidl_handle_t handle) noexcept {
    return AnyTransport(&Transport::VTable, handle);
  }

  AnyTransport(const AnyTransport&) = delete;
  AnyTransport& operator=(const AnyTransport&) = delete;

  AnyTransport(AnyTransport&& other) noexcept : vtable_(other.vtable_), handle_(other.handle_) {
    other.handle_ = FIDL_HANDLE_INVALID;
  }
  AnyTransport& operator=(AnyTransport&& other) noexcept {
    vtable_ = other.vtable_;
    handle_ = other.handle_;
    other.handle_ = FIDL_HANDLE_INVALID;
    return *this;
  }
  ~AnyTransport() {
    if (handle_ != FIDL_HANDLE_INVALID) {
      vtable_->close(handle_);
    }
  }

  constexpr AnyUnownedTransport borrow() const { return AnyUnownedTransport(vtable_, handle_); }

  template <typename Transport>
  typename Transport::UnownedType get() const {
    ZX_ASSERT(vtable_->type == Transport::VTable.type);
    return typename Transport::UnownedType(handle_);
  }

  template <typename Transport>
  typename Transport::OwnedType release() {
    ZX_ASSERT(vtable_->type == Transport::VTable.type);
    fidl_handle_t temp = handle_;
    handle_ = FIDL_HANDLE_INVALID;
    return typename Transport::OwnedType(temp);
  }

  bool is_valid() const { return handle_ != FIDL_HANDLE_INVALID; }

  const TransportVTable* vtable() const { return vtable_; }

  fidl_handle_t handle() const { return handle_; }

  fidl_transport_type type() const { return vtable_->type; }

  zx_status_t write(const WriteOptions& options, const void* data, uint32_t data_count,
                    const fidl_handle_t* handles, const void* handle_metadata,
                    uint32_t handles_count) const {
    return vtable_->write(handle_, options, data, data_count, handles, handle_metadata,
                          handles_count);
  }

  zx_status_t read(const ReadOptions& options, void* data, uint32_t data_capacity,
                   fidl_handle_t* handles, void* handle_metadata, uint32_t handles_capacity,
                   uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) const {
    return vtable_->read(handle_, options, data, data_capacity, handles, handle_metadata,
                         handles_capacity, out_data_actual_count, out_handles_actual_count);
  }

  zx_status_t call(const CallOptions& options, const CallMethodArgs& cargs,
                   uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) const {
    return vtable_->call(handle_, options, cargs, out_data_actual_count, out_handles_actual_count);
  }

#ifdef __Fuchsia__
  zx_status_t create_waiter(async_dispatcher_t* dispatcher,
                            TransportWaitSuccessHandler success_handler,
                            TransportWaitFailureHandler failure_handler,
                            AnyTransportWaiter& any_transport_waiter) const {
    return vtable_->create_waiter(handle_, dispatcher, std::move(success_handler),
                                  std::move(failure_handler), any_transport_waiter);
  }
#endif

 private:
  explicit constexpr AnyTransport(const TransportVTable* vtable, fidl_handle_t handle)
      : vtable_(vtable), handle_(handle) {}

  const TransportVTable* vtable_;
  fidl_handle_t handle_;
};

AnyUnownedTransport MakeAnyUnownedTransport(const AnyTransport& transport);

template <typename TransportObject>
struct AssociatedTransportImpl;

template <typename TransportObject>
using AssociatedTransport = typename AssociatedTransportImpl<std::decay_t<TransportObject>>::type;

}  // namespace internal

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_H_
