// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SOCKET_CONNECT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SOCKET_CONNECT_H_

#include <string>

#include <fbl/unique_fd.h>

namespace zxdb {

class Err;

// If successful, |socket| will contain a valid socket fd.
//
// This function will take care for differences each OS has when connecting through a socket.
Err ConnectToHost(const std::string& host, uint16_t port, fbl::unique_fd* socket);

// If successful, |socket| will contain a valid socket fd.
//
// This function will take care for differences each OS has when connecting to a socket
// located on the filesystem.
Err ConnectToUnixSocket(const std::string& path, fbl::unique_fd* socket);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SOCKET_CONNECT_H_
