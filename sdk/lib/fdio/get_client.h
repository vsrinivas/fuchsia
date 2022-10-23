// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FDIO_GET_CLIENT_H_
#define LIB_FDIO_GET_CLIENT_H_

#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/wire/channel.h>
#include <lib/zx/result.h>

template <class T>
zx::result<typename fidl::WireSyncClient<T>>& get_client() {
  static zx::result<typename fidl::WireSyncClient<T>> client;
  static std::once_flag once;

  std::call_once(once, [&]() {
    client = [&]() -> zx::result<typename fidl::WireSyncClient<T>> {
      auto endpoints = fidl::CreateEndpoints<T>();
      if (endpoints.is_error()) {
        return endpoints.take_error();
      }
      zx_status_t status = fdio_service_connect_by_name(fidl::DiscoverableProtocolName<T>,
                                                        endpoints->server.channel().release());
      if (status != ZX_OK) {
        return zx::error(status);
      }
      return zx::ok(fidl::WireSyncClient(std::move(endpoints->client)));
    }();
  });
  return client;
}

#endif  // LIB_FDIO_GET_CLIENT_H_
