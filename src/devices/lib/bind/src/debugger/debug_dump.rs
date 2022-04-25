// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::compiler::symbol_table::{get_deprecated_key_identifiers, Symbol};
use crate::interpreter::common::BytecodeError;
use crate::interpreter::decode_bind_rules::{DecodedCondition, DecodedInstruction, DecodedRules};

// TODO(fxb/93365): Print the decoded instructions in the composite bytecode.
pub fn dump_bind_rules(bytecode: Vec<u8>) -> Result<String, BytecodeError> {
    match DecodedRules::new(bytecode.clone())? {
        DecodedRules::Normal(decoded_rules) => {
            Ok(dump_instructions(decoded_rules.decoded_instructions))
        }
        DecodedRules::Composite(_) => Ok(bytecode
            .into_iter()
            .map(|byte| format!("{:#04x}", byte))
            .collect::<Vec<String>>()
            .join(", ")),
    }
}

fn dump_condition(cond: DecodedCondition) -> String {
    let op = if cond.is_equal { "==" } else { "!=" };
    let lhs_dump = match cond.lhs {
        Symbol::NumberValue(value) => {
            let deprecated_keys = get_deprecated_key_identifiers();
            match deprecated_keys.get(&(value as u32)) {
                Some(value) => value.clone(),
                None => value.to_string(),
            }
        }
        _ => cond.lhs.to_string(),
    };
    format!("{} {} {}", lhs_dump, op, cond.rhs)
}

// TODO(fxb/93365): Print the label IDs in the jump and label statements.
fn dump_instructions(instructions: Vec<DecodedInstruction>) -> String {
    let mut bind_rules_dump = String::new();
    for inst in instructions {
        let inst_dump = match inst {
            DecodedInstruction::UnconditionalAbort => "  Abort".to_string(),
            DecodedInstruction::Condition(cond) => format!("  {}", dump_condition(cond)),
            DecodedInstruction::Jump(cond) => match cond {
                Some(condition) => format!("  Jump if {} to ??", dump_condition(condition)),
                None => "  Jump to ??".to_string(),
            },
            DecodedInstruction::Label => "  Label ??".to_string(),
        };

        bind_rules_dump.push_str("\n");
        bind_rules_dump.push_str(&inst_dump);
    }

    bind_rules_dump
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::bytecode_constants::*;

    const BIND_HEADER: [u8; 8] = [0x42, 0x49, 0x4E, 0x44, 0x02, 0, 0, 0];

    fn append_section_header(bytecode: &mut Vec<u8>, magic_num: u32, sz: u32) {
        bytecode.extend_from_slice(&magic_num.to_be_bytes());
        bytecode.extend_from_slice(&sz.to_le_bytes());
    }

    #[test]
    fn test_bytecode_print() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 18);

        let str_1: [u8; 5] = [0x57, 0x52, 0x45, 0x4E, 0]; // "WREN"
        bytecode.extend_from_slice(&[1, 0, 0, 0]);
        bytecode.extend_from_slice(&str_1);

        let str_2: [u8; 5] = [0x44, 0x55, 0x43, 0x4B, 0]; // "DUCK"
        bytecode.extend_from_slice(&[2, 0, 0, 0]);
        bytecode.extend_from_slice(&str_2);

        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 28);

        let instructions = [
            0x01, 0x01, 0, 0, 0, 0x05, 0x01, 0x10, 0, 0, 0x10, // 0x05000000 == 0x10000010
            0x11, 0x01, 0, 0, 0, 0x00, 0x01, 0, 0, 0, 0x02, // jmp 1 if key("WREN") == "DUCK"
            0x02, 0, 0, 0,    // jmp 1 if key("WREN") == "DUCK"
            0x30, // abort
            0x20, // jump pad
        ];
        bytecode.extend_from_slice(&instructions);

        let expected_dump =
            "\n  83886080 == 268435472\n  Jump if Key(WREN) == \"DUCK\" to ??\n  Abort\n  Label ??";
        assert_eq!(expected_dump.to_string(), dump_bind_rules(bytecode).unwrap());
    }

    #[test]
    fn test_composite_bytecode_print() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 9);

        let device_name: [u8; 5] = [0x49, 0x42, 0x49, 0x53, 0]; // "IBIS"
        bytecode.extend_from_slice(&[1, 0, 0, 0]);
        bytecode.extend_from_slice(&device_name);

        append_section_header(&mut bytecode, COMPOSITE_MAGIC_NUM, 15);

        // Device name.
        bytecode.extend_from_slice(&[1, 0, 0, 0]);

        bytecode.push(RawNodeType::Primary as u8);
        bytecode.extend_from_slice(&[1, 0, 0, 0]);
        bytecode.extend_from_slice(&[2, 0, 0, 0]);

        let node_inst = [0x30, 0x20];
        bytecode.extend_from_slice(&node_inst);

        let expected_dump = "0x42, 0x49, 0x4e, 0x44, 0x02, 0x00, 0x00, 0x00, 0x53, \
        0x59, 0x4e, 0x42, 0x09, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, \
        0x49, 0x42, 0x49, 0x53, 0x00, 0x43, 0x4f, 0x4d, 0x50, 0x0f, 0x00, \
        0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x50, 0x01, 0x00, 0x00, 0x00, 0x02, \
        0x00, 0x00, 0x00, 0x30, 0x20";
        assert_eq!(expected_dump.to_string(), dump_bind_rules(bytecode).unwrap());
    }
}
