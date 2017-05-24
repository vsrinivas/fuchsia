// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <ddk/device.h>
#include <mxtl/type_support.h>

namespace ddk {
namespace internal {

// base_device is a tag that default initalizes the mx_protocol_device_t so the mixin classes
// can fill in the table.
struct base_device {
  protected:
    base_device() {
        ddk_device_proto_.version = DEVICE_OPS_VERSION;
    }

    mx_protocol_device_t ddk_device_proto_ = {};
    mx_device_t* mxdev_ = nullptr;
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
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "GetProtocolable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkGetProtocol),
                                mx_status_t (D::*)(uint32_t, void**)>::value,
                  "DdkGetProtocol must be a public non-static member function with signature "
                  "'mx_status_t DdkGetProtocol(uint32_t, void**)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_open, DdkOpen);

template <typename D>
constexpr void CheckOpenable() {
    static_assert(has_ddk_open<D>::value, "Openable classes must implement DdkOpen");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "Openable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkOpen),
                                mx_status_t (D::*)(mx_device_t**, uint32_t)>::value,
                  "DdkOpen must be a public non-static member function with signature "
                  "'mx_status_t DdkOpen(mx_device_t**, uint32_t)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_open_at, DdkOpenAt);

template <typename D>
constexpr void CheckOpenAtable() {
    static_assert(has_ddk_open_at<D>::value,
                  "OpenAtable classes must implement DdkOpenAt");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "OpenAtable classes must be derived from ddk::Device<...>.");
    static_assert(
            mxtl::is_same<decltype(&D::DdkOpenAt),
                          mx_status_t (D::*)(mx_device_t**, const char*, uint32_t)>::value,
                  "DdkOpenAt must be a public non-static member function with signature "
                  "'mx_status_t DdkOpenAt(mx_device_t**, const char*, uint32_t)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_close, DdkClose);

template <typename D>
constexpr void CheckClosable() {
    static_assert(has_ddk_close<D>::value,
                  "Closable classes must implement DdkClose");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "Closable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkClose), mx_status_t (D::*)(uint32_t)>::value,
                  "DdkClose must be a public non-static member function with signature "
                  "'mx_status_t DdkClose(uint32)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_unbind, DdkUnbind);

template <typename D>
constexpr void CheckUnbindable() {
    static_assert(has_ddk_unbind<D>::value,
                  "Unbindable classes must implement DdkUnbind");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "Unbindable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkUnbind), void (D::*)(void)>::value,
                  "DdkUnbind must be a public non-static member function with signature "
                  "'void DdkUnbind()'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_release, DdkRelease);

template <typename D>
constexpr void CheckReleasable() {
    static_assert(has_ddk_release<D>::value,
                  "Releasable classes must implement DdkRelease");
    // No need to check is_base_of because Releasable is a property of ddk::Device itself
    static_assert(mxtl::is_same<decltype(&D::DdkRelease), void (D::*)(void)>::value,
                  "DdkRelease must be a public non-static member function with signature "
                  "'void DdkRelease()'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_read, DdkRead);

template <typename D>
constexpr void CheckReadable() {
    static_assert(has_ddk_read<D>::value, "Readable classes must implement DdkRead");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "Readable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkRead),
                                mx_status_t (D::*)(void*, size_t, mx_off_t, size_t*)>::value,
                  "DdkRead must be a public non-static member function with signature "
                  "'mx_status_t DdkRead(void*, size_t, mx_off_t, size_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_write, DdkWrite);

template <typename D>
constexpr void CheckWritable() {
    static_assert(has_ddk_write<D>::value,
                  "Writable classes must implement DdkWrite");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "Writable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkWrite),
                                mx_status_t (D::*)(const void*, size_t, mx_off_t, size_t*)>::value,
                  "DdkWrite must be a public non-static member function with signature "
                  "'mx_status_t DdkWrite(const void*, size_t, mx_off_t, size_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_iotxn_queue, DdkIotxnQueue);

template <typename D>
constexpr void CheckIotxnQueueable() {
    static_assert(has_ddk_iotxn_queue<D>::value,
                  "IotxnQueueable classes must implement DdkIotxnQueue");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "IotxnQueueable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkIotxnQueue), void (D::*)(iotxn_t*)>::value,
                  "DdkIotxnQueue must be a public non-static member function with signature "
                  "'void DdkIotxnQueue(iotxn_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_get_size, DdkGetSize);

template <typename D>
constexpr void CheckGetSizable() {
    static_assert(has_ddk_get_size<D>::value,
                  "GetSizable classes must implement DdkGetSize");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "GetSizable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkGetSize), mx_off_t (D::*)(void)>::value,
                  "DdkGetSize must be a public non-static member function with signature "
                  "'mx_off_t DdkGetSize()'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_ioctl, DdkIoctl);

template <typename D>
constexpr void CheckIoctlable() {
    static_assert(has_ddk_ioctl<D>::value,
                  "Ioctlable classes must implement DdkIoctl");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "Ioctlable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkIoctl),
                                mx_status_t (D::*)(uint32_t, const void*, size_t,
                                                   void*, size_t, size_t*)>::value,
                  "DdkIoctl must be a public non-static member function with signature "
                  "'mx_status_t DdkIoctl(uint32_t, const void*, size_t, void*, size_t, size_t*)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_suspend, DdkSuspend);

template <typename D>
constexpr void CheckSuspendable() {
    static_assert(has_ddk_suspend<D>::value,
                  "Suspendable classes must implement DdkSuspend");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "Suspendable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkSuspend), mx_status_t (D::*)(uint32_t)>::value,
                  "DdkSuspend must be a public non-static member function with signature "
                  "'mx_status_t DdkSuspend(uint32_t)'.");
}

DECLARE_HAS_MEMBER_FN(has_ddk_resume, DdkResume);

template <typename D>
constexpr void CheckResumable() {
    static_assert(has_ddk_resume<D>::value,
                  "Resumable classes must implement DdkResume");
    static_assert(mxtl::is_base_of<base_device, D>::value,
                  "Resumable classes must be derived from ddk::Device<...>.");
    static_assert(mxtl::is_same<decltype(&D::DdkResume), mx_status_t (D::*)(uint32_t)>::value,
                  "DdkResume must be a public non-static member function with signature "
                  "'mx_status_t DdkResume(uint32_t)'.");
}

// all_mixins
//
// Checks a list of types to ensure that all of them are ddk mixins (i.e., they inherit from the
// internal::base_mixin tag).
template <typename Base, typename...>
struct all_mixins : mxtl::true_type {};

template <typename Base, typename Mixin, typename... Mixins>
struct all_mixins<Base, Mixin, Mixins...>
  : mxtl::integral_constant<bool, mxtl::is_base_of<Base, Mixin>::value &&
                                  all_mixins<Base, Mixins...>::value> {};

template <typename... Mixins>
constexpr void CheckMixins() {
    static_assert(all_mixins<base_mixin, Mixins...>::value,
            "All mixins must be from the ddk template library");
}

}  // namespace internal
}  // namespace ddk
