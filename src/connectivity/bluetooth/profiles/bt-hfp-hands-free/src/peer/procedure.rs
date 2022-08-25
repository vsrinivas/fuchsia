// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use at::SerDe;
use at_commands as at;

pub mod slc_initialization;

use slc_initialization::SlcInitProcedure;

#[derive(Debug, Eq, Hash, PartialEq, Clone, Copy)]
pub enum ProcedureMarker {
    /// The Service Level Connection Initialization procedure as defined in HFP v1.8 Section 4.2.
    SlcInitialization,
}

impl ProcedureMarker {
    /// Matches a specific marker to procedure
    pub fn initialize(&self) -> Box<dyn Procedure> {
        match self {
            Self::SlcInitialization => Box::new(SlcInitProcedure::new()),
        }
    }

    pub fn identify_procedure_from_response(
        response: &at::Response,
    ) -> Result<ProcedureMarker, Error> {
        match response {
            at::Response::Success(at::Success::Brsf { .. }) => Ok(Self::SlcInitialization),
            _ => Err(format_err!("Couldn't associate AT response with a known procedure")),
        }
    }
}

pub trait Procedure: Send {
    /// Returns the unique identifier associated with this procedure.
    fn marker(&self) -> ProcedureMarker;

    /// Initial command that will be sent to peer. Not all procedures
    /// will have an initial command.
    fn init_command(&self) -> Option<at::Command> {
        None
    }

    /// Receive an AG `update` to progress the procedure. Returns an error in updating
    /// the procedure or command(s) to be sent back to AG
    ///
    /// `update` is the incoming AT response received from the AG.
    ///
    /// Developers should ensure that the final request of a Procedure does not require
    /// a response.
    fn ag_update(&mut self, _update: at::Response) -> Result<Vec<at::Command>, Error> {
        Ok(vec![])
    }

    /// Returns true if the Procedure is finished.
    fn is_terminated(&self) -> bool {
        false
    }
}

pub fn serialize_to_raw_bytes(command: &mut [at::Command]) -> Result<Vec<u8>, Error> {
    let mut bytes = Vec::new();
    let _ = at::Command::serialize(&mut bytes, command)
        .map_err(|e| format_err!("Could not serialize command {:?}", e));
    Ok(bytes)
}
