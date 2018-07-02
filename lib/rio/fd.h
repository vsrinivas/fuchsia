// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_RIO_FD_H_
#define PERIDOT_LIB_RIO_FD_H_

#include <lib/zx/channel.h>

// Contains utility method to convert between file descriptors and channel
// implementing the remote io protocol.

namespace rio {

// Returns a zx::channel for the given file descriptor.
zx::channel CloneChannel(int fd);

}  // namespace rio

#endif  // PERIDOT_LIB_RIO_FD_H_
