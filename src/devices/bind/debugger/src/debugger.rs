// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::UserError;
use crate::instruction::{DeviceProperty, RawCondition, RawInstruction, RawOp};
use num_traits::FromPrimitive;
use std::fmt;

// From <zircon/driver/binding.h>.
const BIND_FLAGS: u32 = 0;
const BIND_PROTOCOL: u32 = 1;
const BIND_AUTOBIND: u32 = 2;

#[derive(Debug, Clone, PartialEq)]
pub enum DebuggerError {
    BindFlagsNotSupported,
    InvalidCondition,
    InvalidOperation,
    MissingLabel,
    MissingBindProtocol,
    NoOutcome,
}

impl fmt::Display for DebuggerError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", UserError::from(self.clone()))
    }
}

pub fn debug(
    instructions: &[RawInstruction<[u32; 2]>],
    properties: &[DeviceProperty],
) -> Result<bool, DebuggerError> {
    let mut instructions = instructions.iter();

    while let Some(mut instruction) = instructions.next() {
        let condition = FromPrimitive::from_u32(instruction.condition())
            .ok_or(DebuggerError::InvalidCondition)?;
        let operation = FromPrimitive::from_u32(instruction.operation())
            .ok_or(DebuggerError::InvalidOperation)?;

        let condition_succeeds = if condition == RawCondition::Always {
            true
        } else {
            let key = instruction.parameter_b();
            if key == BIND_FLAGS {
                return Err(DebuggerError::BindFlagsNotSupported);
            }

            let device_value = get_device_property(key, properties)?;

            match condition {
                RawCondition::Equal => device_value == instruction.value(),
                RawCondition::NotEqual => device_value != instruction.value(),
                RawCondition::Always => unreachable!(),
            }
        };

        if !condition_succeeds {
            continue;
        }

        match operation {
            RawOp::Abort => return Ok(false),
            RawOp::Match => return Ok(true),
            RawOp::Goto => {
                let label = instruction.parameter_a();
                while !(FromPrimitive::from_u32(instruction.operation()) == Some(RawOp::Label)
                    && instruction.parameter_a() == label)
                {
                    instruction = instructions.next().ok_or(DebuggerError::MissingLabel)?;
                }
            }
            RawOp::Label => (),
        }
    }

    Err(DebuggerError::NoOutcome)
}

fn get_device_property(key: u32, properties: &[DeviceProperty]) -> Result<u32, DebuggerError> {
    for property in properties {
        if property.key == key {
            return Ok(property.value);
        }
    }

    // TODO(45663): The behavior of setting missing properties to 0 is implemented to be consistent
    // with binding.cc. This behavior should eventually be changed to deal with missing properties
    // in a better way.
    match key {
        BIND_PROTOCOL => return Err(DebuggerError::MissingBindProtocol),
        BIND_AUTOBIND => {
            println!(
                "WARNING: Driver has BI_ABORT_IF_AUTOBIND. \
                This bind program will fail in autobind contexts."
            );
            // Set autobind = false, since the debugger is always run with a specific driver.
            Ok(0)
        }
        _ => {
            println!(
                "WARNING: Device has no value for the key 0x{:x}. The value will be set to 0.",
                key
            );
            Ok(0)
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::instruction::{Condition, Instruction};

    #[test]
    fn abort_instruction() {
        let instructions = vec![
            Instruction::Abort(Condition::NotEqual(0x0100, 5)).to_raw(),
            Instruction::Match(Condition::Always).to_raw(),
        ];

        // Aborts when the condition is satisfied.
        let properties = vec![DeviceProperty { key: 0x0100, value: 10 }];
        assert_eq!(debug(&instructions, &properties), Ok(false));

        // Matches when the condition is not satisfied.
        let properties = vec![DeviceProperty { key: 0x0100, value: 5 }];
        assert_eq!(debug(&instructions, &properties), Ok(true));
    }

    #[test]
    fn match_instruction() {
        let instructions = vec![
            Instruction::Match(Condition::Equal(0x0100, 5)).to_raw(),
            Instruction::Abort(Condition::Always).to_raw(),
        ];

        // Matches when the condition is satisfied.
        let properties = vec![DeviceProperty { key: 0x0100, value: 5 }];
        assert_eq!(debug(&instructions, &properties), Ok(true));

        // Aborts when the condition is not satisfied.
        let properties = vec![DeviceProperty { key: 0x0100, value: 10 }];
        assert_eq!(debug(&instructions, &properties), Ok(false));
    }

    #[test]
    fn goto_and_label() {
        let instructions = vec![
            Instruction::Goto(Condition::Equal(0x0100, 5), 1).to_raw(),
            Instruction::Abort(Condition::Always).to_raw(),
            Instruction::Label(1).to_raw(),
            Instruction::Match(Condition::Always).to_raw(),
        ];

        // Jumps and matches when the condition is satisfied.
        let properties = vec![DeviceProperty { key: 0x0100, value: 5 }];
        assert_eq!(debug(&instructions, &properties), Ok(true));

        // Doesn't jump and aborts when the condition is not satisfied.
        let properties = vec![DeviceProperty { key: 0x0100, value: 10 }];
        assert_eq!(debug(&instructions, &properties), Ok(false));
    }

    #[test]
    fn default_value_zero() {
        // When the device doesn't have the property, its value is set to 0.
        let instructions = vec![
            Instruction::Abort(Condition::Equal(0x0100, 0)).to_raw(),
            Instruction::Match(Condition::Always).to_raw(),
        ];
        let properties = Vec::new();
        assert_eq!(debug(&instructions, &properties), Ok(false));

        let instructions = vec![
            Instruction::Abort(Condition::Equal(0x0100, 1)).to_raw(),
            Instruction::Match(Condition::Always).to_raw(),
        ];
        assert_eq!(debug(&instructions, &properties), Ok(true));
    }

    #[test]
    fn autobind() {
        // Autobind is false (BIND_AUTOBIND has the value 0).
        let instructions = vec![
            Instruction::Abort(Condition::NotEqual(BIND_AUTOBIND, 0)).to_raw(),
            Instruction::Match(Condition::Always).to_raw(),
        ];
        let properties = Vec::new();
        assert_eq!(debug(&instructions, &properties), Ok(true));
    }

    #[test]
    fn bind_flags_not_supported() {
        let instructions = vec![Instruction::Abort(Condition::Equal(BIND_FLAGS, 0)).to_raw()];
        let properties = Vec::new();
        assert_eq!(debug(&instructions, &properties), Err(DebuggerError::BindFlagsNotSupported));
    }

    #[test]
    fn missing_label() {
        let instructions = vec![Instruction::Goto(Condition::Always, 5).to_raw()];
        let properties = Vec::new();
        assert_eq!(debug(&instructions, &properties), Err(DebuggerError::MissingLabel));
    }

    #[test]
    fn missing_bind_protocol() {
        let instructions = vec![Instruction::Abort(Condition::Equal(BIND_PROTOCOL, 5)).to_raw()];
        let properties = Vec::new();
        assert_eq!(debug(&instructions, &properties), Err(DebuggerError::MissingBindProtocol));
    }
}
