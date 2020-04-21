// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_zircon as zx, std::mem};

/// Takes ownership of the channel in `chan`. `chan` is replaced with an invalid channel.
pub fn take_channel(chan: &mut zx::Channel) -> zx::Channel {
    let invalid_chan: zx::Channel = zx::Handle::invalid().into();
    mem::replace(chan, invalid_chan)
}
