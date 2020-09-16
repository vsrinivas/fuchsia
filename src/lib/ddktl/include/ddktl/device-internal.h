// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_DDKTL_INCLUDE_DDKTL_DEVICE_INTERNAL_H_
#define SRC_LIB_DDKTL_INCLUDE_DDKTL_DEVICE_INTERNAL_H_

#include <type_traits>

#include <ddk/device.h>
#include <ddktl/init-txn.h>
#include <ddktl/resume-txn.h>
#include <ddktl/suspend-txn.h>
#include <ddktl/unbind-txn.h>
#include <fbl/macros.h>

namespace ddk {
namespace internal {

// Implemented like protocol op mixins, but defined on all base_device since they must implement
template <typename D>
class Releasable {
 public:
  static constexpr void InitOp(zx_protocol_device_t* proto) { proto->release = Release; }

 private:
  static void Release(void* ctx) { return static_cast<D*>(ctx)->DdkRelease(); }
};

// base_device is a tag that default initializes the zx_protocol_device_t so the mixin classes
// can fill in the table.
template <class D, template <typename> class... Mixins>
class base_device : private Mixins<D>... {
 protected:
  explicit base_device(zx_device_t* parent) : parent_(parent) {}

  // populated ops table at compile time. All Mixins must have a constexpr
  // InitOp function that manipulates ops for their respective function
  static const constexpr zx_protocol_device_t ddk_device_proto_ = []() {
    zx_protocol_device_t ops = {};
    ops.version = DEVICE_OPS_VERSION;
    // Releasable Mixin
    Releasable<D>::InitOp(&ops);
    //
    // This C++17 fold expression with the , operator means this ("iterating" at compile time):
    // for (typename Mixin : Mixins<D>) Mixin::Protocol(&ops);
    //
    // template <typename D>
    // class Openable : public base_mixin {
    //   public:
    //     static constexpr void InitOp(zx_protocol_device_t* proto) {
    //         internal::CheckOpenable<D>();
    //         proto->open = Open;
    //     }
    //   private:
    //     static zx_status_t Open(void* ctx, zx_device_t** dev_out, uint32_t flags) {
    //         return static_cast<D*>(ctx)->DdkOpen(dev_out, flags);
    //     }
    // };
    //
    (Mixins<D>::InitOp(&ops), ...);
    return ops;
  }();

  zx_device_t* zxdev_ = nullptr;
  zx_device_t* const parent_;
};

// base_mixin is a tag that all mixins must inherit from.
struct base_mixin {};

// base_protocol is a tag used by protocol implementations
struct base_protocol {
  uint32_t ddk_proto_id_ = 0;
  void* ddk_proto_ops_ = nullptr;

 protected:
  base_protocol() = default;
};

template <typename T>
using is_base_protocol = std::is_base_of<internal::base_protocol, T>;

template <typename T>
using is_base_proto = std::is_same<internal::base_protocol, T>;

// Deprecation helpers: transition a DDKTL protocol interface when there are implementations outside
// of zircon without breaking any builds.
//
// Example:
// template <typename D, bool NewFooMethod=false>
// class MyProtocol : public internal::base_protocol {
//   public:
//     MyProtocol() {
//         internal::CheckMyProtocol<D, NewMethod>();
//         ops_.foo = Foo;
//
//         // Can only inherit from one base_protocol implementation
//         ZX_ASSERT(this->ddk_proto_ops_ == nullptr);
//         ddk_proto_id_ = ZX_PROTOCOL_MY_FOO;
//         ddk_proto_ops_ = &ops_;
//     }
//
//   private:
//     DDKTL_DEPRECATED(NewFooMethod)
//     static Foo(void* ctx, uint32_t options, foo_info_t* info) {
//         return static_cast<D*>(ctx)->MyFoo(options);
//     }
//
//     DDKTL_NOTREADY(NewFooMethod)
//     static Foo(void* ctx, uint32_t options, foo_info_t* info) {
//         return static_cast<D*>(ctx)->MyFoo(options, info);
//     }
// };
//
// // This class hasn't been updated yet, so it uses the default value for NewFooMethod and has the
// // old MyFoo method implementation.
// class MyProtocolImpl : public ddk::Device<MyProtocolImpl, /* other mixins */>,
//                        public ddk::MyProtocol<MyProtocolImpl> {
//   public:
//     zx_status_t MyFoo(uint32_t options);
// };
//
// // The implementation transitions as follows:
// class MyProtocolImpl : public ddk::Device<MyProtocolImpl, /* other mixins */>,
//                        public ddk::MyProtocol<MyProtocolImpl, true> {
//   public:
//     zx_status_t MyFoo(uint32_t options, foo_info_t* info);
// };
//
// Now the DDKTL_DEPRECATED method can be removed, along with the NewFooMethod template parameter.
// At no stage should the build be broken. These annotations may also be used to add new methods, by
// providing a no-op in the DDKTL_DEPRECATED static method.
#define DDKTL_DEPRECATED(condition)              \
  template <typename T = D, bool En = condition, \
            typename std::enable_if<!std::integral_constant<bool, En>::value, int>::type = 0>
#define DDKTL_NOTREADY(condition)                \
  template <typename T = D, bool En = condition, \
            typename std::enable_if<std::integral_constant<bool, En>::value, int>::type = 0>

// Mixin checks: ensure that a type meets the following qualifications:
//
// 1) has a method with the correct name (this makes the compiler errors a little more sane),
// 2) inherits from ddk::Device (by checking that it inherits from ddk::internal::base_device), and
// 3) has the correct method signature.
//
// Note that the 3rd requirement supersedes the first, but the static_assert doesn't even compile if
// the method can't be found, leading to a slightly more confusing error message. Adding the first
// check gives a chance to show the user a more intelligible error message.

DECLARE_HAS_MEMBER_FN(has_ddk_get_protocol, DdkGetProtocol);

template <typename D>
constexpr void CheckGetProtocolable() {
  static_assert(has_ddk_get_protocol<D>::value,
                "GetProtocolable classes must implement DdkGetProtocol");
  static_assert(
      std::is_same<decltype(&D::DdkGetProtocol), zx_status_t (D::*)(uint32_t, void*)>::value,
      "DdkGetProtocol must be a public non-static member function with signature "
      "'zx_status_t DdkGetProtocol(uint32_t, void*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_init, DdkInit);

template <typename D>
constexpr void CheckInitializable() {
  static_assert(has_ddk_init<D>::value, "Init classes must implement DdkInit");
  static_assert(std::is_same<decltype(&D::DdkInit), void (D::*)(InitTxn txn)>::value,
                "DdkInit must be a public non-static member function with signature "
                "'void DdkInit(ddk::InitTxn)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_open, DdkOpen);

template <typename D>
constexpr void CheckOpenable() {
  static_assert(has_ddk_open<D>::value, "Openable classes must implement DdkOpen");
  static_assert(
      std::is_same<decltype(&D::DdkOpen), zx_status_t (D::*)(zx_device_t**, uint32_t)>::value,
      "DdkOpen must be a public non-static member function with signature "
      "'zx_status_t DdkOpen(zx_device_t**, uint32_t)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_close, DdkClose);

template <typename D>
constexpr void CheckClosable() {
  static_assert(has_ddk_close<D>::value, "Closable classes must implement DdkClose");
  static_assert(std::is_same<decltype(&D::DdkClose), zx_status_t (D::*)(uint32_t)>::value,
                "DdkClose must be a public non-static member function with signature "
                "'zx_status_t DdkClose(uint32)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_unbind, DdkUnbind);

template <typename D>
constexpr void CheckUnbindable() {
  static_assert(has_ddk_unbind<D>::value, "Unbindable classes must implement DdkUnbind");
  static_assert(std::is_same<decltype(&D::DdkUnbind), void (D::*)(UnbindTxn txn)>::value,
                "DdkUnbind must be a public non-static member function with signature "
                "'void DdkUnbind(ddk::UnbindTxn)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_release, DdkRelease);

template <typename D>
constexpr void CheckReleasable() {
  static_assert(has_ddk_release<D>::value, "Releasable classes must implement DdkRelease");
  static_assert(std::is_same<decltype(&D::DdkRelease), void (D::*)(void)>::value,
                "DdkRelease must be a public non-static member function with signature "
                "'void DdkRelease()'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_read, DdkRead);

template <typename D>
constexpr void CheckReadable() {
  static_assert(has_ddk_read<D>::value, "Readable classes must implement DdkRead");
  static_assert(std::is_same<decltype(&D::DdkRead),
                             zx_status_t (D::*)(void*, size_t, zx_off_t, size_t*)>::value,
                "DdkRead must be a public non-static member function with signature "
                "'zx_status_t DdkRead(void*, size_t, zx_off_t, size_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_write, DdkWrite);

template <typename D>
constexpr void CheckWritable() {
  static_assert(has_ddk_write<D>::value, "Writable classes must implement DdkWrite");
  static_assert(std::is_same<decltype(&D::DdkWrite),
                             zx_status_t (D::*)(const void*, size_t, zx_off_t, size_t*)>::value,
                "DdkWrite must be a public non-static member function with signature "
                "'zx_status_t DdkWrite(const void*, size_t, zx_off_t, size_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_get_size, DdkGetSize);

template <typename D>
constexpr void CheckGetSizable() {
  static_assert(has_ddk_get_size<D>::value, "GetSizable classes must implement DdkGetSize");
  static_assert(std::is_same<decltype(&D::DdkGetSize), zx_off_t (D::*)(void)>::value ||
                    std::is_same<decltype(&D::DdkGetSize), zx_off_t (D::*)(void) const>::value,
                "DdkGetSize must be a public non-static member function with signature "
                "'zx_off_t DdkGetSize()'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_message, DdkMessage);

template <typename D>
constexpr void CheckMessageable() {
  static_assert(has_ddk_message<D>::value, "Messageable classes must implement DdkMessage");
  static_assert(
      std::is_same<decltype(&D::DdkMessage), zx_status_t (D::*)(fidl_msg_t*, fidl_txn_t*)>::value,
      "DdkMessage must be a public non-static member function with signature "
      "'zx_status_t DdkMessage(fidl_msg_t*, fidl_txn_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_suspend, DdkSuspend);

template <typename D>
constexpr void CheckSuspendable() {
  static_assert(has_ddk_suspend<D>::value, "Suspendable classes must implement DdkSuspend");
  static_assert(std::is_same<decltype(&D::DdkSuspend), void (D::*)(SuspendTxn txn)>::value,
                "DdkSuspend must be a public non-static member function with signature "
                "'zx_status_t DdkSuspend(SuspendTxn txn)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_auto_configure_suspend, DdkConfigureAutoSuspend);

template <typename D>
constexpr void CheckConfigureAutoSuspend() {
  static_assert(has_ddk_auto_configure_suspend<D>::value,
                "ConfigureAutoSuspendable classes must implement DdkConfigureAutoSuspend");
  static_assert(
      std::is_same<decltype(&D::DdkConfigureAutoSuspend), zx_status_t (D::*)(bool, uint8_t)>::value,
      "DdkConfigureAutoSuspend must be a public non-static member function with signature "
      "'zx_status_t DdkConfigureAutoSuspend(bool, uint8_t)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_resume, DdkResume);

template <typename D>
constexpr void CheckResumable() {
  static_assert(has_ddk_resume<D>::value, "Resumable classes must implement DdkResume");
  static_assert(std::is_same<decltype(&D::DdkResume), void (D::*)(ResumeTxn txn)>::value,
                "DdkResume must be a public non-static member function with signature "
                "'void DdkResume(ResumeTxn)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_set_performance_state, DdkSetPerformanceState);

template <typename D>
constexpr void CheckPerformanceTunable() {
  static_assert(has_ddk_set_performance_state<D>::value,
                "PerformanceTunable classes must implement DdkSetPerformanceState");
  static_assert(std::is_same<decltype(&D::DdkSetPerformanceState),
                             zx_status_t (D::*)(uint32_t, uint32_t*)>::value,
                "DdkSetPerformanceState must be a public non-static member function with signature "
                "'zx_status_t DdkSetPerformanceState(uint32_t, uint32_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_rxrpc, DdkRxrpc);

template <typename D>
constexpr void CheckRxrpcable() {
  static_assert(has_ddk_rxrpc<D>::value, "Rxrpcable classes must implement DdkRxrpc");
  static_assert(std::is_same<decltype(&D::DdkRxrpc), zx_status_t (D::*)(uint32_t)>::value,
                "DdkRxrpc must be a public non-static member function with signature "
                "'zx_status_t DdkRxrpc(zx_handle_t)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_child_pre_release, DdkChildPreRelease);

template <typename D>
constexpr void CheckChildPreReleaseable() {
  static_assert(has_ddk_child_pre_release<D>::value,
                "ChildPreReleaseable classes must implement DdkChildPreRelease");
  static_assert(std::is_same<decltype(&D::DdkChildPreRelease), void (D::*)(void*)>::value,
                "DdkChildPreRelease must be a public non-static member function with signature "
                "'void DdkChildPreRelease(void*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_open_protocol_session_multibindable,
                      DdkOpenProtocolSessionMultibindable);
DECLARE_HAS_MEMBER_FN(has_ddk_close_protocol_session_multibindable,
                      DdkCloseProtocolSessionMultibindable);
template <typename D>
constexpr void CheckMultibindable() {
  static_assert(has_ddk_open_protocol_session_multibindable<D>::value,
                "Multibindable classes must implement DdkOpenProtocolSessionMultibindable");
  static_assert(std::is_same<decltype(&D::DdkOpenProtocolSessionMultibindable),
                             zx_status_t (D::*)(uint32_t, void*)>::value,
                "DdkOpenProtocolSessionMultibindable must be a public non-static member function "
                "with signature "
                "'zx_status_t DdkOpenProtocolSessionMultibindable(uint32_t, void*)'.");
  static_assert(has_ddk_close_protocol_session_multibindable<D>::value,
                "Multibindable classes must implement DdkCloseProtocolSessionMultibindable");
  static_assert(std::is_same<decltype(&D::DdkCloseProtocolSessionMultibindable),
                             zx_status_t (D::*)(void*)>::value,
                "DdkCloseProtocolSessionMultibindable must be a public non-static member function "
                "with signature "
                "'zx_status_t DdkCloseProtocolSessionMultibindable(void*)'.");
}

// all_mixins
//
// Checks a list of types to ensure that all of them are ddk mixins (i.e., they inherit from the
// internal::base_mixin tag).
template <typename Base, typename...>
struct all_mixins : std::true_type {};

template <typename Base, typename Mixin, typename... Mixins>
struct all_mixins<Base, Mixin, Mixins...>
    : std::integral_constant<bool, std::is_base_of<Base, Mixin>::value &&
                                       all_mixins<Base, Mixins...>::value> {};

template <typename... Mixins>
constexpr void CheckMixins() {
  static_assert(all_mixins<base_mixin, Mixins...>::value,
                "All mixins must be from the ddk template library");
}

}  // namespace internal
}  // namespace ddk

#endif  // SRC_LIB_DDKTL_INCLUDE_DDKTL_DEVICE_INTERNAL_H_
