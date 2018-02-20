// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zx/channel.h>

#include "lib/fxl/files/unique_fd.h"

namespace media {

// Creates an fdio channel for a file from an fd.
zx::channel ChannelFromFd(fxl::UniqueFD fd);

// Creates an fd from a fdio channel for a file.
fxl::UniqueFD FdFromChannel(zx::channel file_channel);

}  // namespace media
