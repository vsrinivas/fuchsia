// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_USB_DEVICE_UTIL_FIDL_H_
#define SRC_CAMERA_BIN_USB_DEVICE_UTIL_FIDL_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_ptr.h>
#include <lib/fpromise/promise.h>
#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include "src/lib/fsl/handles/object_info.h"

namespace camera {

// Safely unbinds a client connection, doing so on the connection's thread if it differs from the
// caller's thread.
template <class T>
inline void Unbind(fidl::InterfacePtr<T>& p) {
  if (!p) {
    return;
  }

  if (p.dispatcher() == async_get_default_dispatcher()) {
    p.Unbind();
    return;
  }

  async::PostTask(p.dispatcher(), [&]() { p.Unbind(); });
}

template <typename T, typename Enable = void>
struct IsFidlChannelWrapper : std::false_type {};

template <typename T>
struct IsFidlChannelWrapper<fidl::InterfaceHandle<T>> : std::true_type {};

template <typename T>
struct IsFidlChannelWrapper<fidl::InterfacePtr<T>> : std::true_type {};

template <typename T>
struct IsFidlChannelWrapper<fidl::SynchronousInterfacePtr<T>> : std::true_type {};

template <typename T>
struct IsFidlChannelWrapper<fidl::InterfaceRequest<T>> : std::true_type {};

template <typename T>
struct IsFidlChannelWrapper<fidl::Binding<T>> : std::true_type {};

template <typename T>
inline constexpr bool IsFidlChannelWrapperV = IsFidlChannelWrapper<T>::value;

template <typename T>
inline zx_handle_t GetFidlChannelHandle(const T& fidl) {
  static_assert(IsFidlChannelWrapperV<T>, "'fidl' must be one one of the fidl channel wrappers");
  return fidl.channel().get();
}

template <typename T>
inline zx_koid_t GetKoid(const T& fidl) {
  return fsl::GetKoid(GetFidlChannelHandle(fidl));
}

template <typename T>
inline zx_koid_t GetRelatedKoid(const T& fidl) {
  return fsl::GetRelatedKoid(GetFidlChannelHandle(fidl));
}

template <typename T>
inline std::pair<zx_koid_t, zx_koid_t> GetKoids(const T& fidl) {
  return fsl::GetKoids(GetFidlChannelHandle(fidl));
}

}  // namespace camera

#endif  // SRC_CAMERA_BIN_USB_DEVICE_UTIL_FIDL_H_
