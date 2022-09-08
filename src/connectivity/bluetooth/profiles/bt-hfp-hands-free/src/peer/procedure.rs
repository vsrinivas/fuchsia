// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use at_commands as at;

pub mod phone_status;
pub mod slc_initialization;

use phone_status::PhoneStatusProcedure;
use slc_initialization::SlcInitProcedure;

use super::service_level_connection::SharedState;

#[derive(Debug, Eq, Hash, PartialEq, Clone, Copy)]
pub enum ProcedureMarker {
    /// The Service Level Connection Initialization procedure as defined in HFP v1.8 Section 4.2.
    SlcInitialization,
    /// The Transfer of Phone Status procedures as defined in HFP v1.8 Section 4.4 - 4.7.
    PhoneStatus,
}

impl ProcedureMarker {
    /// Matches a specific marker to procedure
    pub fn initialize(&self) -> Box<dyn Procedure> {
        match self {
            Self::SlcInitialization => Box::new(SlcInitProcedure::new()),
            Self::PhoneStatus => Box::new(PhoneStatusProcedure::new()),
        }
    }

    /// Returns the procedure identifier based on AT response.
    pub fn identify_procedure_from_response(
        initialized: bool,
        response: &at::Response,
    ) -> Result<ProcedureMarker, Error> {
        if !initialized {
            match response {
                at::Response::Success(at::Success::Brsf { .. })
                | at::Response::Success(at::Success::Cind { .. })
                | at::Response::RawBytes(_)
                | at::Response::Ok => Ok(Self::SlcInitialization),
                _ => Err(format_err!(
                    "Non-SLCI AT response received when SLCI has not completed: {:?}",
                    response
                )),
            }
        } else {
            match response {
                _ => Err(format_err!("Other procedures not implemented yet.")),
            }
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
    fn ag_update(
        &mut self,
        _state: &mut SharedState,
        _update: &Vec<at::Response>,
    ) -> Result<Vec<at::Command>, Error> {
        Ok(vec![])
    }

    /// Returns true if the Procedure is finished.
    fn is_terminated(&self) -> bool {
        false
    }
}
