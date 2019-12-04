// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/mount_channel.h>

#include <fs/vfs.h>

namespace fs {

MountChannel::~MountChannel() {
  if (channel_.is_valid()) {
    Vfs::UnmountHandle(std::move(channel_), zx::time::infinite_past());
  }
}

}  // namespace fs
