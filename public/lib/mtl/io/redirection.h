// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_IO_REDIRECTION_H_
#define LIB_MTL_IO_REDIRECTION_H_

#include <mx/handle.h>
#include <mx/socket.h>

#include "lib/fxl/fxl_export.h"

namespace mtl {

// Contains a startup handle used to provide a handle to process during launch.
struct StartupHandle {
  // The startup handle id, as described by <magenta/processargs.h>.
  uint32_t id;

  // The startup handle value.
  mx::handle handle;
};

// Creates a socket and returns one end of it along with a |StartupHandle|
// which can be used to bind the other end to a file descriptor in a new
// process which is about to be launched.
//
// |startup_fd| is the file descriptor number to which the socket will be
// bound in the new process, eg. use 0 to bind to standard input.
// |out_socket| receives the local end of the socket.
// |out_startup_handle| receives the startup handle which must be passed to
// the launcher to bind the remote end of the socket in the new process.
FXL_EXPORT mx_status_t
CreateRedirectedSocket(int startup_fd,
                       mx::socket* out_socket,
                       StartupHandle* out_startup_handle);

}  // namespace mtl

#endif  // LIB_MTL_IO_REDIRECTION_H_
