// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_clone::FIDLClone;
use fidl_fuchsia_bluetooth::PeerId;
use fidl_fuchsia_bluetooth_sys::Peer;

// Changed peers for bluetooth access watch.
pub type ChangedPeers = (Vec<Peer>, Vec<PeerId>);

// Custom clone trait for if these types need to be cloned.
pub trait Clone {
    fn clone(&self) -> Self;
}

impl Clone for ChangedPeers {
    fn clone(&self) -> Self {
        (self.0.clone(), self.1.clone())
    }
}
