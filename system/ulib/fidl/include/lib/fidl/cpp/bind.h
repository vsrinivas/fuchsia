// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BIND_H_
#define LIB_FIDL_CPP_BIND_H_

#include <lib/async/dispatcher.h>
#include <lib/fidl/bind.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

namespace fidl {
namespace internal {

template <typename>
struct MemberFunctionTraits;

template <typename R, typename T, typename... Args>
struct MemberFunctionTraits<R (T::*)(Args...)> {
    typedef T instance_type;
};

} // namespace internal

// Bind a member of a base class to a FIDL dispatch function, to allow
// compatibility with the C bindings.
//
// For a FIDL function:
//
//   1: MyFunction(Args) -> (ReturnValue);
//
// The following C binding will be generated:
//
//   zx_status_t fidl_MyInterfaceMyFunction(void* ctx, Args... args, fidl_txn_t* txn);
//
// If BindOps has been invoked, then "ctx" is guaranteed to
// be the templated type T.
//
// Invoking the following:
//
//   BindMember<&MyClass::MyFunction>
//
// Will instantiate a function with a signature matching the C binding, but which
// allows the method to be invoked on the class.
template <auto Fn,
          typename T = typename internal::MemberFunctionTraits<decltype(Fn)>::instance_type,
          typename... Args>
static zx_status_t BindMember(void* ctx, Args... args) {
    auto instance = static_cast<T*>(ctx);
    return (instance->*Fn)(static_cast<decltype(args)&&>(args)...);
}

// A utility function which simplifies binding methods of derived methods to FIDL
// dispatch functions.
//
// A typical use case would look like the following:
//
// FIDL:
//
//   1: MyFunction(Args) -> (ReturnValue);
//
// C++:
//
//   class MyClass {
//   public:
//        zx_status_t FunctionImplementation(Args, fidl_txn_t* txn) {
//            ...;
//        }
//
//        zx_status_t Bind(async_dispatcher_t* dispatcher, zx::channel channel) {
//            static constexpr Interface_ops_t kOps = {
//                .MyFunction = BindMember<&MyClass::FunctionImplementation>,
//            };
//            return fidl::BindOps<Dispatch>(dispatcher, channel, this, &kOps);
//        }
//   }
//
//   ...
//
//   MyClass instance;
//   instance.Bind(dispatcher, channel);
template <auto Dispatch, typename Ops, typename T>
zx_status_t BindOps(async_dispatcher_t* dispatcher, zx::channel channel, T* ctx, const Ops* ops) {
    // TODO(smklein): Re-enable this check when std is available in Zircon.
    // static_assert(std::is_same<decltype(Dispatch),
    //                            zx_status_t (*)(void*, fidl_txn_t*, fidl_msg_t*, const Ops* ops)
    //                           >::value, "Invalid dispatch function");
    return fidl_bind(dispatcher, channel.release(),
                     reinterpret_cast<fidl_dispatch_t*>(Dispatch), ctx, ops);
}

} // namespace fidl

#endif // LIB_FIDL_CPP_BIND_H_
