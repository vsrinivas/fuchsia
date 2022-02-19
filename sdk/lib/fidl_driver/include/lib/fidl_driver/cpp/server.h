// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_SERVER_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_SERVER_H_

#include <lib/fidl/llcpp/server.h>
#include <lib/fidl_driver/cpp/internal/server_details.h>
#include <lib/fidl_driver/cpp/transport.h>

namespace fdf {

// This class manages a server connection over an fdf channel and its binding to
// an |fdf_dispatcher_t*|, which may be multi-threaded. See the detailed
// documentation on the |BindServer| APIs.
template <typename Protocol>
class ServerBindingRef : public fidl::internal::ServerBindingRefBase {
 public:
  using ServerBindingRefBase::ServerBindingRefBase;

  // Triggers an asynchronous unbind operation. If specified, |on_unbound| will be invoked on a
  // dispatcher thread, passing in the channel and the unbind reason. On return, the dispatcher
  // will no longer have any wait associated with the channel (though handling of any already
  // in-flight transactions will continue).
  //
  // This may be called from any thread.
  //
  // WARNING: While it is safe to invoke Unbind() from any thread, it is unsafe to wait on the
  // OnUnboundFn from a dispatcher thread, as that will likely deadlock.
  using ServerBindingRefBase::Unbind;
};

// |BindServer| starts handling message on |server_end| using implementation
// |impl|, on a potentially multi-threaded |dispatcher|. Multiple requests may
// be concurrently in-flight, and responded to synchronously or asynchronously.
//
// The behavior of |fdf::BindServer| is identical to |fidl::BindServer|, the
// specialization for channels. Please see documentation in channel.h for more
// details.
template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServer(
    fdf_dispatcher_t* dispatcher, ServerEnd<typename ServerImpl::_EnclosingProtocol> server_end,
    ServerImpl* impl, OnUnbound&& on_unbound = nullptr) {
  static_assert(std::is_same_v<typename ServerImpl::_EnclosingProtocol::Transport,
                               fidl::internal::DriverTransport>);
  return fidl::internal::BindServerImpl<ServerImpl>(
      fdf_dispatcher_get_async_dispatcher(dispatcher), std::move(server_end), impl,
      fidl::internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}

// Overload of |BindServer| that takes ownership of the server as a |unique_ptr|.
// The pointer is destroyed on the same thread as the one calling |on_unbound|,
// and happens right after |on_unbound|.
//
// The behavior of |fdf::BindServer| is identical to |fidl::BindServer|, the
// specialization for channels. Please see documentation in channel.h for more
// details.
template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServer(
    fdf_dispatcher_t* dispatcher, ServerEnd<typename ServerImpl::_EnclosingProtocol> server_end,
    std::unique_ptr<ServerImpl>&& impl, OnUnbound&& on_unbound = nullptr) {
  static_assert(std::is_same_v<typename ServerImpl::_EnclosingProtocol::Transport,
                               fidl::internal::DriverTransport>);
  ServerImpl* impl_raw = impl.get();
  return fidl::internal::BindServerImpl<ServerImpl>(
      fdf_dispatcher_get_async_dispatcher(dispatcher), std::move(server_end), impl_raw,
      fidl::internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}

// Overload of |BindServer| that shares ownership of the server via a |shared_ptr|.
// The pointer is destroyed on the same thread as the one calling |on_unbound|,
// and happens right after |on_unbound|.
//
// The behavior of |fdf::BindServer| is identical to |fidl::BindServer|, the
// specialization for channels. Please see documentation in channel.h for more
// details.
template <typename ServerImpl, typename OnUnbound = std::nullptr_t>
ServerBindingRef<typename ServerImpl::_EnclosingProtocol> BindServer(
    fdf_dispatcher_t* dispatcher, ServerEnd<typename ServerImpl::_EnclosingProtocol> server_end,
    std::shared_ptr<ServerImpl> impl, OnUnbound&& on_unbound = nullptr) {
  static_assert(std::is_same_v<typename ServerImpl::_EnclosingProtocol::Transport,
                               fidl::internal::DriverTransport>);
  ServerImpl* impl_raw = impl.get();
  return fidl::internal::BindServerImpl<ServerImpl>(
      fdf_dispatcher_get_async_dispatcher(dispatcher), std::move(server_end), impl_raw,
      fidl::internal::UnboundThunk(std::move(impl), std::forward<OnUnbound>(on_unbound)));
}
}  // namespace fdf

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_SERVER_H_
