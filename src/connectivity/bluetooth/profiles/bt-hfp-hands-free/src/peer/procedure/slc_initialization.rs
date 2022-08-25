// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use at_commands as at;

use super::{Procedure, ProcedureMarker};

/// TODO (fxbug.dev/104703): Still work in progress
pub struct SlcInitProcedure {
    responded: bool,
}

impl SlcInitProcedure {
    pub fn new() -> Self {
        Self { responded: false }
    }
}

impl Procedure for SlcInitProcedure {
    fn marker(&self) -> ProcedureMarker {
        ProcedureMarker::SlcInitialization
    }

    fn init_command(&self) -> Option<at::Command> {
        Some(at::Command::Brsf { features: 0 })
    }

    fn ag_update(&mut self, _update: at_commands::Response) -> Result<Vec<at::Command>, Error> {
        Ok(vec![at::Command::CindRead {}])
    }

    fn is_terminated(&self) -> bool {
        self.responded
    }
}
