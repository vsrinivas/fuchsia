// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_UTIL_FILE_CHANNEL_H_
#define GARNET_BIN_MEDIA_UTIL_FILE_CHANNEL_H_

#include <lib/zx/channel.h>

#include "lib/fxl/files/unique_fd.h"

namespace media {

// Creates an fdio channel for a file from an fd. A null channel is returned
// on failure.
zx::channel ChannelFromFd(fxl::UniqueFD fd);

// Creates an fd from a fdio channel for a file. An invalid UniqueFD is returned
// on failure.
fxl::UniqueFD FdFromChannel(zx::channel file_channel);

}  // namespace media

#endif  // GARNET_BIN_MEDIA_UTIL_FILE_CHANNEL_H_
