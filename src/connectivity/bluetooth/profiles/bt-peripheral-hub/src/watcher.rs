// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_bluetooth_power::WatcherRequestStream;
use std::sync::Arc;

use crate::peripheral_state::PeripheralState;

/// Represents a handler for a client connection to the `fuchsia.bluetooth.power.Watcher` FIDL
/// capability.
pub struct Watcher {
    _shared_state: Arc<PeripheralState>,
}

impl Watcher {
    pub fn new(_shared_state: Arc<PeripheralState>) -> Self {
        Self { _shared_state }
    }

    pub async fn run(self, _stream: WatcherRequestStream) -> Result<(), Error> {
        todo!("Process `power.Watch` requests");
    }
}
