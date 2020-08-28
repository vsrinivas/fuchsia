// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Server Channels are 5 bits wide; they are the 5 most significant bits of the DLCI.
/// Server Channels 0 and 31 are reserved. See RFCOMM 5.4.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct ServerChannel(pub u8);

impl ServerChannel {
    const MAX: ServerChannel = ServerChannel(30);
    const MIN: ServerChannel = ServerChannel(1);

    /// Returns an iterator over all the Server Channels.
    pub fn all() -> impl Iterator<Item = ServerChannel> {
        (Self::MIN.0..=Self::MAX.0).map(|x| ServerChannel(x))
    }
}
