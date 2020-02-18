// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Generating C constants from bind programs

#![allow(dead_code)]

use crate::instruction::{Condition, Instruction};

fn c_macro_invocation(macro_name: &str, arguments: &Vec<String>) -> String {
    let mut parts = vec![macro_name, "("];
    let argument_string = arguments.join(", ");
    parts.push(&argument_string);
    parts.push(")");
    parts.join("")
}

fn to_macro_parts(condition: &Condition) -> Vec<String> {
    match condition {
        Condition::Always => vec!["AL".to_string()],
        Condition::Equal(b, v) => vec!["EQ".to_string(), b.to_string(), v.to_string()],
        Condition::NotEqual(b, v) => vec!["NE".to_string(), b.to_string(), v.to_string()],
    }
}

fn to_c_constant(instruction: &Instruction) -> String {
    match instruction {
        Instruction::Abort(condition) => {
            c_macro_invocation("BI_ABORT_IF", &to_macro_parts(condition))
        }
        Instruction::Match(condition) => {
            c_macro_invocation("BI_MATCH_IF", &to_macro_parts(condition))
        }
        Instruction::Goto(condition, a) => {
            let mut parts = to_macro_parts(condition);
            parts.push(a.to_string());
            c_macro_invocation("BI_GOTO_IF", &parts)
        }
        Instruction::Label(a) => c_macro_invocation("BI_LABEL", &vec![a.to_string()]),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_abort_value() {
        let instruction = Instruction::Abort(Condition::Always);
        let c_constant = to_c_constant(&instruction);
        assert_eq!(c_constant, "BI_ABORT_IF(AL)")
    }

    fn test_abort_if_value() {
        let instruction = Instruction::Abort(Condition::Equal(2, 3));
        let c_constant = to_c_constant(&instruction);
        assert_eq!(c_constant, "BI_ABORT_IF(EQ, 2, 3)")
    }

    #[test]
    fn test_match_value() {
        let instruction = Instruction::Match(Condition::Always);
        let c_constant = to_c_constant(&instruction);
        assert_eq!(c_constant, "BI_MATCH_IF(AL)")
    }

    #[test]
    fn test_match_if_value() {
        let instruction = Instruction::Match(Condition::Equal(18, 19));
        let c_constant = to_c_constant(&instruction);
        assert_eq!(c_constant, "BI_MATCH_IF(EQ, 18, 19)")
    }

    #[test]
    fn test_goto_value() {
        let instruction = Instruction::Goto(Condition::Always, 42);
        let c_constant = to_c_constant(&instruction);
        assert_eq!(c_constant, "BI_GOTO_IF(AL, 42)")
    }

    #[test]
    fn test_goto_if_value() {
        let instruction = Instruction::Goto(Condition::NotEqual(5, 6), 55);
        let c_constant = to_c_constant(&instruction);
        assert_eq!(c_constant, "BI_GOTO_IF(NE, 5, 6, 55)")
    }

    #[test]
    fn test_label_value() {
        let instruction = Instruction::Label(23);
        let c_constant = to_c_constant(&instruction);
        assert_eq!(c_constant, "BI_LABEL(23)")
    }
}
