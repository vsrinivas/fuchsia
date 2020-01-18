// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_UTIL_H_
#define GARNET_BIN_TRACE_MANAGER_UTIL_H_

#include <fuchsia/tracing/controller/cpp/fidl.h>
#include <lib/zx/socket.h>

#include <iosfwd>

namespace tracing {

namespace controller = ::fuchsia::tracing::controller;

enum class TransferStatus {
  // The transfer is complete.
  kComplete,
  // An error was detected with the provider, ignore its contribution to
  // trace output.
  kProviderError,
  // Writing of trace data to the receiver failed in an unrecoverable way.
  kWriteError,
  // The receiver of the transfer went away.
  kReceiverDead,
};

std::ostream& operator<<(std::ostream& out, TransferStatus status);

std::ostream& operator<<(std::ostream& out, controller::BufferDisposition disposition);

std::ostream& operator<<(std::ostream& out, controller::SessionState state);

// Writes |len| bytes from |buffer| to |socket|. Returns
// TransferStatus::kComplete if the entire buffer has been
// successfully transferred. A return value of
// TransferStatus::kReceiverDead indicates that the peer was closed
// during the transfer.
TransferStatus WriteBufferToSocket(const zx::socket& socket, const void* buffer, size_t len);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_UTIL_H_
