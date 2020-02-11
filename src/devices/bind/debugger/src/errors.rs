// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::debugger::DebuggerError;
use std::fmt;

pub struct UserError {
    index: String,
    message: String,
    is_compiler_bug: bool,
}

impl UserError {
    fn new(index: &str, message: &str, is_compiler_bug: bool) -> Self {
        UserError { index: index.to_string(), message: message.to_string(), is_compiler_bug }
    }
}

impl fmt::Display for UserError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        writeln!(f, "[{}]: {}", self.index, self.message)?;
        if self.is_compiler_bug {
            writeln!(f, "This is a bind compiler bug, please report it!")?;
        }
        Ok(())
    }
}

impl From<DebuggerError> for UserError {
    fn from(error: DebuggerError) -> Self {
        match error {
            DebuggerError::BindFlagsNotSupported => {
                UserError::new("E001", "The BIND_FLAGS property is not supported.", false)
            }
            DebuggerError::InvalidCondition => {
                UserError::new("E002", "The bind program contained an invalid condition.", true)
            }
            DebuggerError::InvalidOperation => {
                UserError::new("E003", "The bind program contained an invalid operation.", true)
            }
            DebuggerError::MissingLabel => UserError::new(
                "E004",
                "The bind program contained a GOTO with no matching LABEL.",
                false,
            ),
            DebuggerError::MissingBindProtocol => UserError::new(
                "E005",
                concat!(
                    "Device doesn't have a BIND_PROTOCOL property. ",
                    "The outcome of the bind program would depend on the device's protocol_id."
                ),
                false,
            ),
            DebuggerError::NoOutcome => UserError::new(
                "E006",
                "Reached the end of the instructions without binding or aborting.",
                true,
            ),
        }
    }
}
