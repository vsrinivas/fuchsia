// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/time.h>

#include <string>

#include "fbl/unique_fd.h"
#include "src/developer/shell/mirror/common.h"
#include "src/developer/shell/mirror/wire_format.h"

#ifndef SRC_DEVELOPER_SHELL_MIRROR_CLIENT_H_
#define SRC_DEVELOPER_SHELL_MIRROR_CLIENT_H_

namespace shell::mirror::client {

// Manages the connection with a server.
class ClientConnection {
 public:
  // Initializes a connection on the given host and port,
  // which is declared [<ipv6-address>]:<port>
  Err Init(const std::string& host_and_port);

  // Loads the files at the given |path| on the server into |files|.
  Err Load(Files* files, struct timeval* timeout = nullptr);

  // Kills the server.  Use with caution!
  Err KillServer();

 private:
  fbl::unique_fd socket_;
};

}  // namespace shell::mirror::client

#endif  // SRC_DEVELOPER_SHELL_MIRROR_CLIENT_H_
