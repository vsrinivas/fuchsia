// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_LLCPP_CONNECT_SERVICE_H_
#define LIB_FIDL_LLCPP_CONNECT_SERVICE_H_

#include <lib/fidl/llcpp/string_view.h>
#include <lib/fit/result.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#endif  // __Fuchsia__

namespace fidl {

// A FIDL-specific result type. Alias of fit::result<T, E> for FIDL result types, whose error (E) is
// always zx_status_t.
template <typename T = void>
using result = fit::result<T, zx_status_t>;

#ifdef __Fuchsia__

// A wrapper around a Zircon channel, strongly-typed on a FIDL protocol.
template <typename FidlProtocol>
class ClientChannel final {
 public:
  explicit ClientChannel(zx::channel channel) : channel_(std::move(channel)) {}

  // Moves the underlying Zircon channel out to the caller. Once called, this object
  // should no longer be used.
  zx::channel take_channel() { return std::move(channel_); }

  // Returns a reference to the underlying Zircon channel.
  const zx::channel& channel() const { return channel_; }

 private:
  zx::channel channel_;
};

// Creates a synchronous FIDL client for the FIDL protocol `FidlProtocol`, bound to the
// given channel.
template <typename FidlProtocol>
typename FidlProtocol::SyncClient BindSyncClient(ClientChannel<FidlProtocol> channel) {
  return typename FidlProtocol::SyncClient(channel.take_channel());
}

namespace internal {

// The method signature required to implement the method that issues the Directory::Open
// FIDL call for a Service's member protocol.
using ConnectMemberFunc = zx_status_t (*)(zx::unowned_channel service_dir,
                                          fidl::StringView member_name, zx::channel channel);

}  // namespace internal

#endif  // __Fuchsia__

}  // namespace fidl

#endif  // LIB_FIDL_LLCPP_CONNECT_SERVICE_H_
