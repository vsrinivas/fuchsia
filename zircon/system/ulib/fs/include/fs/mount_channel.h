// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#include <fs/client.h>
#endif  // __Fuchsia__

#include <utility>

namespace fs {

#ifdef __Fuchsia__

// MountChannel functions exactly the same as a channel, except that it
// intentionally destructs by sending a clean "shutdown" signal to the
// underlying filesystem. Up until the point that a remote handle is
// attached to a vnode, this wrapper guarantees not only that the
// underlying handle gets closed on error, but also that the sub-filesystem
// is released (which cleans up the underlying connection to the block
// device).
class MountChannel {
 public:
  constexpr MountChannel() = default;
  explicit MountChannel(zx_handle_t handle) : channel_(handle) {}
  explicit MountChannel(zx::channel channel) : channel_(std::move(channel)) {}
  MountChannel(MountChannel&& other) : channel_(std::move(other.channel_)) {}

  zx::channel TakeChannel() { return std::move(channel_); }

  ~MountChannel() {
    if (channel_.is_valid()) {
      vfs_unmount_handle(channel_.release(), 0);
    }
  }

 private:
  zx::channel channel_;
};

#endif  // __Fuchsia__

}  // namespace fs
