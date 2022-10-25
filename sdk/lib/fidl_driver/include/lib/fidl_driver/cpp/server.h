// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_SERVER_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_SERVER_H_

#include <lib/fidl/cpp/wire/server.h>
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

  // Triggers an asynchronous unbind operation. If specified, |on_unbound| will
  // be asynchronously run on a dispatcher thread, passing in the endpoint and
  // the unbind reason.
  //
  // On return, the dispatcher will stop monitoring messages on the endpoint,
  // though handling of any already in-flight transactions will continue.
  // Pending completers may be discarded.
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

// |ServerBinding| binds the implementation of a FIDL protocol to a server
// endpoint.
//
// |ServerBinding| listens for incoming messages on the channel, decodes them,
// and calls the appropriate method on the bound implementation.
//
// When the |ServerBinding| object is destroyed, the binding between the
// protocol endpoint and the server implementation is torn down and the channel
// is closed. Once destroyed, it will not make any method calls on the server
// implementation. Thus the idiomatic usage of a |ServerBinding| is to embed it
// as a member variable of a server implementation, such that they are destroyed
// together.
//
// ## Example
//
//  class Impl : public fdf::Server<fuchsia_my_library::MyProtocol> {
//   public:
//    Impl(fdf::ServerEnd<fuchsia_my_library::Protocol> server_end, fdf_dispatcher_t* dispatcher)
//        : binding_(dispatcher, std::move(server_end), this, std::mem_fn(&Impl::OnFidlClosed)) {}
//
//    void OnFidlClosed(fidl::UnbindInfo info) override {
//      // Handle errors..
//    }
//
//    // More method implementations omitted...
//
//   private:
//    fdf::ServerBinding<fuchsia_my_library::MyProtocol> binding_;
//  };
//
// ## See also
//
//  * |WireClient|, |Client|: which are the client analogues of this class.
//
// ## Thread safety
//
// |ServerBinding| is thread unsafe. Tearing down a |ServerBinding| guarantees
// no more method calls on the borrowed |Impl|. This is only possible when
// the teardown is synchronized with message dispatch. The binding will enforce
// [synchronization guarantees][synchronization-guarantees] at runtime with
// threading checks.
//
// [synchronization-guarantees]:
// https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/zircon/system/ulib/async/README.md#verifying-synchronization-requirements
template <typename FidlProtocol>
class ServerBinding final : public ::fidl::internal::ServerBindingBase<FidlProtocol> {
 private:
  using Base = ::fidl::internal::ServerBindingBase<FidlProtocol>;

 public:
  // |CloseHandler| is invoked when the endpoint managed by the |ServerBinding|
  // is closed, due to a terminal error or because the user initiated binding
  // teardown.
  //
  // |CloseHandler| is silently discarded if |ServerBinding| is destroyed, to
  // avoid calling into a destroyed server implementation.
  //
  // The handler may have one of these signatures:
  //
  //     void(fidl::UnbindInfo info);
  //     void(Impl* impl, fidl::UnbindInfo info);
  //
  // |info| contains the detailed reason for stopping message dispatch.
  // |impl| is the pointer to the server implementation borrowed by the binding.
  //
  // The second overload allows one to bind the close handler to an instance
  // method on the server implementation, without capturing extra state:
  //
  //     class Impl : fdf::WireServer<Protocol> {
  //      public:
  //       void OnFidlClosed(fidl::UnbindInfo) { /* handle errors */ }
  //     };
  //
  //     fidl::ServerBinding<Protocol> binding(
  //         dispatcher, std::move(server_end), impl,
  //         std::mem_fn(&Impl::OnFidlClosed));
  //
  template <typename Impl, typename CloseHandler>
  static void CloseHandlerRequirement() {
    Base::template CloseHandlerRequirement<Impl, CloseHandler>();
  }

  // Constructs a binding that dispatches messages from |server_end| to |impl|,
  // using |dispatcher|.
  //
  // |Impl| should implement |fdf::Server<FidlProtocol>| or
  // |fdf::WireServer<FidlProtocol>|.
  //
  // |impl| and any state captured in |error_handler| should outlive the bindings.
  // It's not safe to move |impl| while the binding is still referencing it.
  //
  // |close_handler| is invoked when the endpoint managed by the |ServerBinding|
  // is closed, due to a terminal error or because the user initiated binding
  // teardown. See |CloseHandlerRequirement| for details on the error handler.
  template <typename Impl, typename CloseHandler>
  ServerBinding(fdf_dispatcher_t* dispatcher, ServerEnd<FidlProtocol> server_end, Impl* impl,
                CloseHandler&& close_handler)
      : Base(fdf_dispatcher_get_async_dispatcher(dispatcher), std::move(server_end), impl,
             std::forward<CloseHandler>(close_handler)) {}

  // The usual usage style of |ServerBinding| puts it as a member variable of a
  // server object, to which it unsafely borrows. Thus it's unsafe to move the
  // server objects. As a precaution, we do not allow moving the bindings. If
  // one needs to move a server object, consider wrapping it in a
  // |std::unique_ptr|.
  ServerBinding(ServerBinding&& other) noexcept = delete;
  ServerBinding& operator=(ServerBinding&& other) noexcept = delete;

  ServerBinding(const ServerBinding& other) noexcept = delete;
  ServerBinding& operator=(const ServerBinding& other) noexcept = delete;

  // Tears down the binding and closes the connection.
  //
  // After the binding destructs, it will release references on |impl|.
  // Destroying the binding will discard the |close_handler| without calling it.
  ~ServerBinding() = default;
};

}  // namespace fdf

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_SERVER_H_
