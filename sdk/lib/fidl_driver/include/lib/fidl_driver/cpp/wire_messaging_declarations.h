// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_WIRE_MESSAGING_DECLARATIONS_H_
#define LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_WIRE_MESSAGING_DECLARATIONS_H_

namespace fdf {

template <typename FidlMethod>
class WireUnownedResult;

// WireServer is a pure-virtual interface to be implemented by a server.
template <typename FidlMethod>
class WireServer;

// WireAsyncEventHandler is used by asynchronous clients to handle events and
// adds a callback for to report fatal errors.
template <typename Protocol>
class WireAsyncEventHandler;

}  // namespace fdf

#endif  // LIB_FIDL_DRIVER_INCLUDE_LIB_FIDL_DRIVER_CPP_WIRE_MESSAGING_DECLARATIONS_H_
