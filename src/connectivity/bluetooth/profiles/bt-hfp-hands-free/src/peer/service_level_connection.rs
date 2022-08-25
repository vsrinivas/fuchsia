// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use at_commands as at;
use std::collections::hash_map::HashMap;

use super::procedure::{Procedure, ProcedureMarker};

pub struct SlcState {
    pub procedures: HashMap<ProcedureMarker, Box<dyn Procedure>>,
}

// TODO(fxbug.dev/104703): More fields for SLCI
impl SlcState {
    pub fn new() -> Self {
        Self { procedures: HashMap::new() }
    }

    pub fn process_at_response(
        &mut self,
        response: &at::Response,
    ) -> Result<ProcedureMarker, Error> {
        let procedure_id = ProcedureMarker::identify_procedure_from_response(&response)?;
        let _ = self.procedures.entry(procedure_id).or_insert(procedure_id.initialize());
        Ok(procedure_id)
    }
}
