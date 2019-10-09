// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STATUSOR_ENDPOINT_OR_ERROR_H_
#define LIB_STATUSOR_ENDPOINT_OR_ERROR_H_

#include <lib/zx/channel.h>

#include <variant>

// Both a handle to the server side of a channel and a llcpp endpoint.
template <typename T>
class Endpoint {
 public:
  Endpoint(zx::channel server, zx::channel client)
      : internal_(std::move(server), T(std::move(client))) {}

  zx::channel TakeServer() { return std::move(internal_.first); }
  T& operator*() { return internal_.second; }
  T* operator->() { return &internal_.second; }

 private:
  std::pair<zx::channel, T> internal_;
};

// This class tries to create a zx::channel for an llcpp endpoint, and either succeeds and contains
// an zx::channel for the server side and a client side class instance, or fails and contains a
// zx_status_t.
template <typename T>
class EndpointOrError {
 public:
  static EndpointOrError<T> Create() {
    zx::channel token_server, token_client;
    zx_status_t status = zx::channel::create(0, &token_server, &token_client);
    if (status != ZX_OK) {
      return EndpointOrError<T>(status);
    }
    return EndpointOrError<T>(std::move(token_server), std::move(token_client));
  }

  bool ok() { return internal_.index() == 1; }
  zx_status_t status() { return std::get<0>(internal_); }

  Endpoint<T> ValueOrDie() {
    ZX_DEBUG_ASSERT(ok());
    return std::get<1>(std::move(internal_));
  }

 private:
  explicit EndpointOrError(zx_status_t status) : internal_(status) {}
  EndpointOrError(zx::channel server, zx::channel client)
      : internal_(Endpoint<T>(std::move(server), std::move(client))) {}

  std::variant<zx_status_t, Endpoint<T>> internal_;
};

#endif  // LIB_STATUSOR_ENDPOINT_OR_ERROR_H_
