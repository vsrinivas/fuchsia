// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DRIVER2_PROMISE_H_
#define SRC_DEVICES_LIB_DRIVER2_PROMISE_H_

#include <lib/fpromise/promise.h>

#include "src/devices/lib/driver2/namespace.h"

namespace driver {

namespace internal {

// Connects to the given |path| in |ns|, and returns a fpromise::result containing a
// fidl::WireSharedClient on success.
template <typename T>
fpromise::result<fidl::WireSharedClient<T>, zx_status_t> ConnectWithResult(
    const driver::Namespace& ns, async_dispatcher_t* dispatcher, std::string_view path,
    uint32_t flags) {
  auto result = ns.Connect<T>(path, flags);
  if (result.is_error()) {
    return fpromise::error(result.status_value());
  }
  fidl::WireSharedClient<T> client(std::move(*result), dispatcher);
  return fpromise::ok(std::move(client));
}

}  // namespace internal

// Connects to the given |path| in |ns|, and returns a fpromise::promise containing a
// fidl::WireSharedClient on success.
template <typename T>
fpromise::promise<fidl::WireSharedClient<T>, zx_status_t> Connect(
    const driver::Namespace& ns, async_dispatcher_t* dispatcher,
    std::string_view path = fidl::DiscoverableProtocolDefaultPath<T>,
    uint32_t flags = ZX_FS_RIGHT_READABLE) {
  return fpromise::make_result_promise(internal::ConnectWithResult<T>(ns, dispatcher, path, flags));
}

}  // namespace driver

#endif  // SRC_DEVICES_LIB_DRIVER2_PROMISE_H_
