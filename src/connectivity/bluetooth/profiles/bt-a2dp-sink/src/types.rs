// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_avdtp::PeerManagerControlHandle;

/// State for storing the control handle for PeerManager
/// We need this to reply back to bt-avdtp-tool
/// The control handle reply only happens after a peer has been connected
/// (ProfileEvent::OnPeerConnected)
/// The control handle is created when we create the PeerManager listener
/// TODO(fxb/37089): Save the control_handle for each peer so that the channel will shutdown
/// when a peer disconnects from a2dp. Clients can then listen to this and clean up state.
#[derive(Debug)]
pub struct ControlHandleManager {
    pub handle: Option<PeerManagerControlHandle>,
}

impl ControlHandleManager {
    pub fn new() -> Self {
        Self { handle: None }
    }

    pub fn insert(&mut self, handle: PeerManagerControlHandle) {
        if self.handle.is_none() {
            self.handle = Some(handle);
        }
    }
}
