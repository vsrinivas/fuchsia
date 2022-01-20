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

extern "C" typedef struct async_dispatcher async_dispatcher_t;

namespace fidl {

namespace internal {
struct TransportVTable;

class TransportContextBase {
 public:
  TransportContextBase() = default;
  TransportContextBase(const TransportContextBase&) = delete;
  TransportContextBase& operator=(const TransportContextBase&) = delete;

  TransportContextBase(TransportContextBase&& other) : vtable_(other.vtable_), data_(other.data_) {
    other.vtable_ = nullptr;
    other.data_ = nullptr;
  }
  TransportContextBase& operator=(TransportContextBase&& other) {
    if (this == &other) {
      return *this;
    }

    vtable_ = other.vtable_;
    data_ = other.data_;

    other.vtable_ = nullptr;
    other.data_ = nullptr;

    return *this;
  }

 protected:
  TransportContextBase(const TransportVTable* vtable, void* data) : vtable_(vtable), data_(data) {}

  void* release(const TransportVTable* vtable);

  const TransportVTable* vtable_ = nullptr;
  void* data_ = nullptr;
};

class IncomingTransportContext final : public TransportContextBase {
 public:
  IncomingTransportContext() = default;
  IncomingTransportContext(IncomingTransportContext&&) = default;
  IncomingTransportContext& operator=(IncomingTransportContext&&) = default;
  ~IncomingTransportContext();

  template <typename Transport>
  static IncomingTransportContext Create(typename Transport::IncomingTransportContextType* value) {
    return IncomingTransportContext(&Transport::VTable, value);
  }

  template <typename Transport>
  typename Transport::IncomingTransportContextType* release() {
    return static_cast<typename Transport::IncomingTransportContextType*>(
        TransportContextBase::release(&Transport::VTable));
  }

 private:
  IncomingTransportContext(const TransportVTable* vtable, void* data)
      : TransportContextBase(vtable, data) {}
};

class OutgoingTransportContext final : public TransportContextBase {
 public:
  OutgoingTransportContext() = default;
  OutgoingTransportContext(OutgoingTransportContext&&) = default;
  OutgoingTransportContext& operator=(OutgoingTransportContext&&) = default;
  ~OutgoingTransportContext();

  template <typename Transport>
  static OutgoingTransportContext Create(typename Transport::OutgoingTransportContextType* value) {
    return OutgoingTransportContext(&Transport::VTable, value);
  }

  template <typename Transport>
  typename Transport::OutgoingTransportContextType* release() {
    return static_cast<typename Transport::OutgoingTransportContextType*>(
        TransportContextBase::release(&Transport::VTable));
  }

 private:
  OutgoingTransportContext(const TransportVTable* vtable, void* data)
      : TransportContextBase(vtable, data) {}
};

}  // namespace internal

// Options passed from the user-facing write API to transport write().
struct WriteOptions {
  // Transport specific context.
  internal::OutgoingTransportContext outgoing_transport_context;
};

// Options passed from the user-facing read API to transport read().
struct ReadOptions {
  bool discardable = false;
  // Transport specific context populated by read.
  internal::IncomingTransportContext* out_incoming_transport_context;
};

// Options passed from the user-facing call API to transport call().
struct CallOptions {
  zx_time_t deadline = ZX_TIME_INFINITE;

  // Transport specific context.
  internal::OutgoingTransportContext outgoing_transport_context;
  // Transport specific context populated by call.
  internal::IncomingTransportContext* out_incoming_transport_context;
};

class IncomingMessage;

namespace internal {

struct CallMethodArgs {
  const void* wr_data;
  const fidl_handle_t* wr_handles;
  const fidl_handle_metadata_t* wr_handle_metadata;
  uint32_t wr_data_count;
  uint32_t wr_handles_count;

  // Exactly one of rd_* or out_rd_* will be populated.
  // If Transport::TransportProvidesReadBuffer is true, the out_rd_* will be populated.
  // Otherwise, rd_* will be populated.

  void* rd_data;
  fidl_handle_t* rd_handles;
  fidl_handle_metadata_t* rd_handle_metadata;
  uint32_t rd_data_capacity;
  uint32_t rd_handles_capacity;

  void** out_rd_data;
  fidl_handle_t** out_rd_handles;
  fidl_handle_metadata_t** out_rd_handle_metadata;
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
using AnyTransportWaiter = NonMovableAny<TransportWaiter, /* kCapacity= */ 256ull>;

// Function receiving notification of successful waits on a TransportWaiter.
using TransportWaitSuccessHandler =
    fit::inline_function<void(fidl::IncomingMessage&, IncomingTransportContext transport_context)>;

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
  zx_status_t (*write)(fidl_handle_t handle, WriteOptions options, const void* data,
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
  zx_status_t (*call)(fidl_handle_t handle, CallOptions options, const CallMethodArgs& cargs,
                      uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count);

  // Create a waiter object to wait for messages on the transport.
  // No waits are started initially on the waiter. Call Begin() to start waiting.
  // The waiter object is output into |any_transport_waiter|.
  zx_status_t (*create_waiter)(fidl_handle_t handle, async_dispatcher_t* dispatcher,
                               TransportWaitSuccessHandler success_handler,
                               TransportWaitFailureHandler failure_handler,
                               AnyTransportWaiter& any_transport_waiter);

  // Close the handle.
  void (*close)(fidl_handle_t);

  // Closes incoming/outgoing transport context contents.
  // Set to nullptr if no close function is needed.
  void (*close_incoming_transport_context)(void*);
  void (*close_outgoing_transport_context)(void*);
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

  zx_status_t write(WriteOptions options, const void* data, uint32_t data_count,
                    const fidl_handle_t* handles, const void* handle_metadata,
                    uint32_t handles_count) const {
    return vtable_->write(handle_, std::move(options), data, data_count, handles, handle_metadata,
                          handles_count);
  }

  zx_status_t read(const ReadOptions& options, void* data, uint32_t data_capacity,
                   fidl_handle_t* handles, void* handle_metadata, uint32_t handles_capacity,
                   uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) const {
    return vtable_->read(handle_, options, data, data_capacity, handles, handle_metadata,
                         handles_capacity, out_data_actual_count, out_handles_actual_count);
  }

  zx_status_t call(CallOptions options, const CallMethodArgs& cargs,
                   uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) const {
    return vtable_->call(handle_, std::move(options), cargs, out_data_actual_count,
                         out_handles_actual_count);
  }

  zx_status_t create_waiter(async_dispatcher_t* dispatcher,
                            TransportWaitSuccessHandler success_handler,
                            TransportWaitFailureHandler failure_handler,
                            AnyTransportWaiter& any_transport_waiter) const {
    return vtable_->create_waiter(handle_, dispatcher, std::move(success_handler),
                                  std::move(failure_handler), any_transport_waiter);
  }

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

  zx_status_t write(WriteOptions options, const void* data, uint32_t data_count,
                    const fidl_handle_t* handles, const void* handle_metadata,
                    uint32_t handles_count) const {
    return vtable_->write(handle_, std::move(options), data, data_count, handles, handle_metadata,
                          handles_count);
  }

  zx_status_t read(const ReadOptions& options, void* data, uint32_t data_capacity,
                   fidl_handle_t* handles, void* handle_metadata, uint32_t handles_capacity,
                   uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) const {
    return vtable_->read(handle_, options, data, data_capacity, handles, handle_metadata,
                         handles_capacity, out_data_actual_count, out_handles_actual_count);
  }

  zx_status_t call(CallOptions options, const CallMethodArgs& cargs,
                   uint32_t* out_data_actual_count, uint32_t* out_handles_actual_count) const {
    return vtable_->call(handle_, std::move(options), cargs, out_data_actual_count,
                         out_handles_actual_count);
  }

  zx_status_t create_waiter(async_dispatcher_t* dispatcher,
                            TransportWaitSuccessHandler success_handler,
                            TransportWaitFailureHandler failure_handler,
                            AnyTransportWaiter& any_transport_waiter) const {
    return vtable_->create_waiter(handle_, dispatcher, std::move(success_handler),
                                  std::move(failure_handler), any_transport_waiter);
  }

 private:
  explicit constexpr AnyTransport(const TransportVTable* vtable, fidl_handle_t handle)
      : vtable_(vtable), handle_(handle) {}

  const TransportVTable* vtable_;
  fidl_handle_t handle_;
};

template <typename TransportObject>
struct AssociatedTransportImpl;

template <typename TransportObject>
using AssociatedTransport = typename AssociatedTransportImpl<std::decay_t<TransportObject>>::type;

template <typename T>
AnyTransport MakeAnyTransport(T transport) {
  return AnyTransport::Make<AssociatedTransport<T>>(transport.release());
}

template <typename T>
AnyUnownedTransport MakeAnyUnownedTransport(const T& transport) {
  if constexpr (std::is_same_v<T, AnyTransport>) {
    return transport.borrow();
  } else if constexpr (std::is_same_v<T, typename AssociatedTransport<T>::OwnedType>) {
    return AnyUnownedTransport::Make<AssociatedTransport<T>>(transport.get());
  } else {
    static_assert(std::is_same_v<T, typename AssociatedTransport<T>::UnownedType>);
    return AnyUnownedTransport::Make<AssociatedTransport<T>>(transport->get());
  }
}

struct DriverTransport;
struct ChannelTransport;

// The ClientEnd type for a given protocol, e.g. fidl::ClientEnd or fdf::ClientEnd.
template <typename Protocol>
using ClientEndType = typename Protocol::Transport::template ClientEnd<Protocol>;

// The UnownedClientEnd type for a given protocol, e.g. fidl::UnownedClientEnd.
template <typename Protocol>
using UnownedClientEndType = typename Protocol::Transport::template UnownedClientEnd<Protocol>;

// The ServerEnd type for a given protocol, e.g. fidl::ServerEnd or fdf::ServerEnd.
template <typename Protocol>
using ServerEndType = typename Protocol::Transport::template ServerEnd<Protocol>;

// The ServerBindingRef type for a given protocol, e.g. fidl::ServerBindingRef.
template <typename Protocol>
using ServerBindingRefType = typename Protocol::Transport::template ServerBindingRef<Protocol>;

// The WireUnownedResult type for a given method, e.g. fidl::WireUnownedResult.
template <typename FidlMethod>
using WireUnownedResultType =
    typename FidlMethod::Protocol::Transport::template WireUnownedResult<FidlMethod>;

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_INTERNAL_TRANSPORT_H_
