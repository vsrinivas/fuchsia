// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_bluetooth_power::ReporterRequestStream;
use std::sync::Arc;

use crate::peripheral_state::PeripheralState;

/// Represents a handler for a client connection to the `fuchsia.bluetooth.power.Reporter` FIDL
/// capability.
pub struct Reporter {
    _shared_state: Arc<PeripheralState>,
}

impl Reporter {
    pub fn new(_shared_state: Arc<PeripheralState>) -> Self {
        Self { _shared_state }
    }

    pub async fn run(self, _stream: ReporterRequestStream) -> Result<(), Error> {
        todo!("Process `power.Report` requests");
    }
}
