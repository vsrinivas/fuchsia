// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::compiler::{BindProgram, Symbol, SymbolTable};
use std::collections::HashMap;

/// Functions for encoding the new bytecode format. When the
/// old bytecode format is deleted, the "v2" should be removed from the names.

// Magic number for BIND.
const BIND_MAGIC_NUM: u32 = 0x42494E44;

// Magic number for SYMB.
const SYMB_MAGIC_NUM: u32 = 0x53594E42;

// Magic number for INST.
const INSTRUCTION_MAGIC_NUM: u32 = 0x494E5354;

const BYTECODE_VERSION: u32 = 2;

// Appends the section header to the bytecode.
fn section_with_header(magic_num: u32, section_bytecode: &mut Vec<u8>) -> Vec<u8> {
    let mut header_bytecode = magic_num.to_be_bytes().to_vec();
    header_bytecode.append(&mut (section_bytecode.len() as u32).to_le_bytes().to_vec());
    header_bytecode.append(section_bytecode);
    header_bytecode
}

#[allow(dead_code)]
struct Encoder {
    symbol_table: SymbolTable,
    encoded_symbols: HashMap<String, u32>,
}

impl Encoder {
    pub fn new(bind_program: BindProgram) -> Self {
        Encoder {
            symbol_table: bind_program.symbol_table,
            encoded_symbols: HashMap::<String, u32>::new(),
        }
    }

    pub fn encode_to_bytecode(mut self) -> Vec<u8> {
        let mut bytecode: Vec<u8> = vec![];

        // Encode the header.
        bytecode.extend_from_slice(&BIND_MAGIC_NUM.to_be_bytes());
        bytecode.extend_from_slice(&BYTECODE_VERSION.to_le_bytes());

        // Encode the symbol table.
        let mut symbol_table = section_with_header(SYMB_MAGIC_NUM, &mut self.encode_symbol_table());
        bytecode.append(&mut symbol_table);

        // Encode the instruction section.
        let mut instruction_section = section_with_header(INSTRUCTION_MAGIC_NUM, &mut vec![]);
        bytecode.append(&mut instruction_section);

        bytecode
    }

    fn encode_symbol_table(&mut self) -> Vec<u8> {
        let mut bytecode: Vec<u8> = vec![];
        let mut unique_id: u32 = 1;

        // TODO(fxb/67919): Add support for enum values.
        // TODO(fxb/67920): Add error handling to encode_to_bytecode(). Return an error if
        // the string exceeds the max length of 255.
        for value in self.symbol_table.values() {
            if let Symbol::StringValue(str) = value {
                // The strings in the symbol table contain fully qualified namespace, so
                // it's safe to assume that it won't contain duplicates in production.
                // However, as a precaution, panic if that happens.
                // TODO(fxb/67920): Return an error if a duplicate string is found.
                if self.encoded_symbols.contains_key(&str.to_string()) {
                    panic!("duplicate string in symbol table");
                }

                self.encoded_symbols.insert(str.to_string(), unique_id);
                bytecode.extend_from_slice(&unique_id.to_le_bytes());

                // Encode the string followed by a zero terminator.
                bytecode.append(&mut str.to_string().into_bytes());
                bytecode.push(0);

                unique_id += 1;
            }
        }

        bytecode
    }
}

pub fn encode_to_bytecode_v2(bind_program: BindProgram) -> Vec<u8> {
    Encoder::new(bind_program).encode_to_bytecode()
}

pub fn encode_to_string_v2(_bind_program: BindProgram) -> String {
    // Unimplemented, new bytecode is not implemented yet. See fxb/67440.
    "".to_string()
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::bind_library::ValueType;
    use crate::compiler::{SymbolicInstruction, SymbolicInstructionInfo};
    use crate::make_identifier;
    use crate::parser_common::CompoundIdentifier;
    use std::collections::HashMap;

    const BIND_MAGIC_NUM_BYTECODE: [u8; 4] = [0x42, 0x49, 0x4E, 0x44];
    const BIND_VERSION_BYTECODE: [u8; 4] = [2, 0, 0, 0];

    const SYMB_MAGIC_NUM_BYTECODE: [u8; 4] = [0x53, 0x59, 0x4e, 0x42];
    const INSTRUCTION_MAGIC_NUM_BYTECODE: [u8; 4] = [0x49, 0x4e, 0x53, 0x54];

    struct BytecodeChecker {
        iter: std::vec::IntoIter<u8>,
    }

    impl BytecodeChecker {
        pub fn new(bind_program: BindProgram) -> Self {
            let bytecode = encode_to_bytecode_v2(bind_program);
            BytecodeChecker { iter: bytecode.into_iter() }
        }

        pub fn verify_next_u8(&mut self, expected: u8) {
            assert_eq!(expected, self.iter.next().unwrap());
        }

        // Verify and advance the iterator to the next four bytes.
        pub fn verify_next_u32(&mut self, expected: [u8; 4]) {
            for n in 0..4 {
                self.verify_next_u8(expected[n]);
            }
        }

        // Verify that the next bytes matches the string and advance
        // the iterator.
        pub fn verify_string(&mut self, expected: String) {
            expected.chars().for_each(|c| self.verify_next_u8(c as u8));
            self.verify_next_u8(0);
        }

        // Verify that the iterator reached the end of the bytecode.
        pub fn verify_end(&mut self) {
            assert_eq!(None, self.iter.next());
        }
    }

    #[test]
    fn test_symbol_table() {
        let mut symbol_table: SymbolTable = HashMap::new();

        symbol_table
            .insert(make_identifier!("cupwing"), Symbol::StringValue("wren-babbler".to_string()));
        symbol_table.insert(make_identifier!("shoveler"), Symbol::NumberValue(0));
        symbol_table.insert(make_identifier!("scoter"), Symbol::BoolValue(false));
        symbol_table.insert(make_identifier!("goldeneye"), Symbol::EnumValue);
        symbol_table.insert(make_identifier!("bufflehead"), Symbol::DeprecatedKey(0));
        symbol_table.insert(
            make_identifier!("canvasback"),
            Symbol::Key("redhead".to_string(), ValueType::Number),
        );
        symbol_table
            .insert(make_identifier!("nightjar"), Symbol::StringValue("frogmouth".to_string()));

        let symbol_table_values: Vec<String> = symbol_table
            .values()
            .filter_map(|symbol| match symbol {
                Symbol::StringValue(str) => Some(str.clone()),
                _ => None,
            })
            .collect();

        let bind_program = BindProgram {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalAbort,
            }],
            symbol_table: symbol_table,
        };

        let mut checker = BytecodeChecker::new(bind_program);

        // Verify the header.
        checker.verify_next_u32(BIND_MAGIC_NUM_BYTECODE);
        checker.verify_next_u32(BIND_VERSION_BYTECODE);

        // Verify symbol table. Only the symbols with string values should
        // be in the table.
        checker.verify_next_u32(SYMB_MAGIC_NUM_BYTECODE);
        checker.verify_next_u32([31, 0, 0, 0]);

        let mut unique_id = 1;
        symbol_table_values.into_iter().for_each(|value| {
            checker.verify_next_u32([unique_id, 0, 0, 0]);
            checker.verify_string(value);
            unique_id += 1;
        });

        // Verify the instruction section.
        checker.verify_next_u32(INSTRUCTION_MAGIC_NUM_BYTECODE);
        checker.verify_next_u32([0, 0, 0, 0]);

        checker.verify_end();
    }

    #[test]
    fn test_empty_symbol_table() {
        let bind_program = BindProgram {
            instructions: vec![SymbolicInstructionInfo {
                location: None,
                instruction: SymbolicInstruction::UnconditionalAbort,
            }],
            symbol_table: HashMap::new(),
        };

        let mut checker = BytecodeChecker::new(bind_program);

        // Verify the header.
        checker.verify_next_u32(BIND_MAGIC_NUM_BYTECODE);
        checker.verify_next_u32(BIND_VERSION_BYTECODE);

        // Verify symbol table. Only the symbols with string values should
        // be in the table.
        checker.verify_next_u32(SYMB_MAGIC_NUM_BYTECODE);
        checker.verify_next_u32([0, 0, 0, 0]);

        // Verify the instruction section.
        checker.verify_next_u32(INSTRUCTION_MAGIC_NUM_BYTECODE);
        checker.verify_next_u32([0, 0, 0, 0]);

        checker.verify_end();
    }
}
