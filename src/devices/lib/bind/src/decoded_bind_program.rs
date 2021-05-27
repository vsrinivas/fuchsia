// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bind_program_v2_constants::*;
use crate::bytecode_common::*;
use std::collections::HashMap;

// Each section header contains a uint32 magic number and a uint32 value.
const HEADER_SZ: usize = 8;

// At minimum, the bytecode would contain the bind header, symbol table
// header and instruction header.
const MINIMUM_BYTECODE_SZ: usize = HEADER_SZ * 3;

// This struct decodes and unwraps the given bytecode into a symbol table
// and list of instructions.
#[derive(Debug, PartialEq, Clone)]
pub struct DecodedProgram {
    pub symbol_table: HashMap<u32, String>,
    pub instructions: Vec<u8>,
}

impl DecodedProgram {
    pub fn new(mut bytecode: Vec<u8>) -> Result<Self, BytecodeError> {
        if bytecode.len() < MINIMUM_BYTECODE_SZ {
            return Err(BytecodeError::UnexpectedEnd);
        }

        // Verify the header and size of each section to ensure that the
        // section bytecode doesn't overflow to the next section.
        let version = verify_and_read_header(&bytecode, BIND_MAGIC_NUM)?;
        if version != BYTECODE_VERSION {
            return Err(BytecodeError::InvalidVersion(version));
        }

        // Separate the symbol table and instruction bytecode and verify the magic
        // number and length.
        let mut symbol_table_bytecode = bytecode.split_off(HEADER_SZ);
        let symbol_table_sz =
            verify_and_read_header(&symbol_table_bytecode, SYMB_MAGIC_NUM)? as usize;
        if symbol_table_bytecode.len() < symbol_table_sz + (HEADER_SZ * 2) {
            return Err(BytecodeError::IncorrectSectionSize);
        }

        // Separate the instruction bytecode out of the symbol table bytecode and verify
        // the magic number and length.
        let inst_bytecode = symbol_table_bytecode.split_off(HEADER_SZ + symbol_table_sz);
        let inst_sz = verify_and_read_header(&inst_bytecode, INSTRUCTION_MAGIC_NUM)? as usize;
        if inst_bytecode.len() != HEADER_SZ + inst_sz {
            return Err(BytecodeError::IncorrectSectionSize);
        }

        Ok(DecodedProgram {
            symbol_table: read_symbol_table(symbol_table_bytecode)?,
            instructions: read_instructions(inst_bytecode)?,
        })
    }
}

fn get_u32_bytes(bytecode: &Vec<u8>, idx: usize) -> Result<[u8; 4], BytecodeError> {
    if idx + 4 > bytecode.len() {
        return Err(BytecodeError::UnexpectedEnd);
    }

    let mut bytes: [u8; 4] = [0; 4];
    for i in 0..4 {
        bytes[i] = bytecode[idx + i];
    }
    Ok(bytes)
}

// Verify the header magic number and return the next value in the header.
fn verify_and_read_header(bytecode: &Vec<u8>, magic_num: u32) -> Result<u32, BytecodeError> {
    let parsed_magic_num = u32::from_be_bytes(get_u32_bytes(bytecode, 0)?);
    if parsed_magic_num != magic_num {
        return Err(BytecodeError::InvalidHeader(magic_num, parsed_magic_num));
    }

    Ok(u32::from_le_bytes(get_u32_bytes(bytecode, 4)?))
}

fn skip_header(iter: &mut BytecodeIter) -> Result<(), BytecodeError> {
    if iter.nth(HEADER_SZ - 1).is_none() {
        return Err(BytecodeError::UnexpectedEnd);
    }

    Ok(())
}

fn read_string(iter: &mut BytecodeIter) -> Result<String, BytecodeError> {
    let mut str_bytes = vec![];

    // Read in the bytes for the string until a zero terminator is reached.
    // If the number of bytes exceed the maximum string length, return an error.
    loop {
        let byte = next_u8(iter)?;
        if byte == 0 {
            break;
        }

        str_bytes.push(byte);
        if str_bytes.len() > MAX_STRING_LENGTH {
            return Err(BytecodeError::InvalidStringLength);
        }
    }

    if str_bytes.is_empty() {
        return Err(BytecodeError::EmptyString);
    }

    String::from_utf8(str_bytes).map_err(|_| BytecodeError::Utf8ConversionFailure)
}

fn read_symbol_table(bytecode: Vec<u8>) -> Result<HashMap<u32, String>, BytecodeError> {
    let section_sz = verify_and_read_header(&bytecode, SYMB_MAGIC_NUM)? as usize;
    if bytecode.len() < section_sz + HEADER_SZ {
        return Err(BytecodeError::IncorrectSectionSize);
    }

    let mut iter = bytecode.into_iter();
    skip_header(&mut iter)?;

    let mut symbol_table = HashMap::new();
    let mut expected_key = SYMB_TBL_START_KEY;
    while let Some(key) = try_next_u32(&mut iter)? {
        if key != expected_key {
            return Err(BytecodeError::InvalidSymbolTableKey(key));
        }

        // Read the string and increase the byte count by the string length and the
        // zero terminator.
        let str_val = read_string(&mut iter)?;
        symbol_table.insert(key, str_val);
        expected_key += 1;
    }

    Ok(symbol_table)
}

fn read_instructions(bytecode: Vec<u8>) -> Result<Vec<u8>, BytecodeError> {
    let section_sz = verify_and_read_header(&bytecode, INSTRUCTION_MAGIC_NUM)? as usize;
    if bytecode.len() < section_sz + HEADER_SZ {
        return Err(BytecodeError::IncorrectSectionSize);
    }

    let mut iter = bytecode.into_iter();
    skip_header(&mut iter)?;
    Ok(iter.collect::<Vec<u8>>())
}

#[cfg(test)]
mod test {
    use super::*;

    const BIND_HEADER: [u8; 8] = [0x42, 0x49, 0x4E, 0x44, 0x02, 0, 0, 0];

    fn append_section_header(bytecode: &mut Vec<u8>, magic_num: u32, sz: u32) {
        bytecode.extend_from_slice(&magic_num.to_be_bytes());
        bytecode.extend_from_slice(&sz.to_le_bytes());
    }

    #[test]
    fn test_invalid_header() {
        // Test invalid magic number.
        let mut bytecode: Vec<u8> = vec![0x41, 0x49, 0x4E, 0x44, 0x02, 0, 0, 0];
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 0);
        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);
        assert_eq!(
            Err(BytecodeError::InvalidHeader(BIND_MAGIC_NUM, 0x41494E44)),
            DecodedProgram::new(bytecode)
        );

        // Test invalid version.
        let mut bytecode: Vec<u8> = vec![0x42, 0x49, 0x4E, 0x44, 0x03, 0, 0, 0];
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 0);
        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);
        assert_eq!(Err(BytecodeError::InvalidVersion(3)), DecodedProgram::new(bytecode));

        // Test invalid symbol table header.
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, 0xAAAAAAAA, 0);
        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);
        assert_eq!(
            Err(BytecodeError::InvalidHeader(SYMB_MAGIC_NUM, 0xAAAAAAAA)),
            DecodedProgram::new(bytecode)
        );

        // Test invalid instruction header.
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 0);
        append_section_header(&mut bytecode, 0xAAAAAAAA, 0);
        assert_eq!(
            Err(BytecodeError::InvalidHeader(INSTRUCTION_MAGIC_NUM, 0xAAAAAAAA)),
            DecodedProgram::new(bytecode)
        );
    }

    #[test]
    fn test_long_string() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();

        let mut long_str: [u8; 275] = [0x41; 275];
        long_str[274] = 0;

        let symbol_section_sz = long_str.len() + 4;
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, symbol_section_sz as u32);

        bytecode.extend_from_slice(&[1, 0, 0, 0]);
        bytecode.extend_from_slice(&long_str);

        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);
        assert_eq!(Err(BytecodeError::InvalidStringLength), DecodedProgram::new(bytecode));
    }

    #[test]
    fn test_unexpected_end() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        bytecode.extend_from_slice(&SYMB_MAGIC_NUM.to_be_bytes());
        assert_eq!(Err(BytecodeError::UnexpectedEnd), DecodedProgram::new(bytecode));
    }

    #[test]
    fn test_string_with_no_zero_terminator() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 14);

        let invalid_str: [u8; 10] = [0x41; 10];
        bytecode.extend_from_slice(&[1, 0, 0, 0]);
        bytecode.extend_from_slice(&invalid_str);

        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);

        assert_eq!(Err(BytecodeError::UnexpectedEnd), DecodedProgram::new(bytecode));
    }

    #[test]
    fn test_duplicate_key() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 14);

        let str_1: [u8; 3] = [0x41, 0x42, 0];
        bytecode.extend_from_slice(&[1, 0, 0, 0]);
        bytecode.extend_from_slice(&str_1);

        let str_2: [u8; 3] = [0x42, 0x43, 0];
        bytecode.extend_from_slice(&[1, 0, 0, 0]);
        bytecode.extend_from_slice(&str_2);

        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);
        assert_eq!(Err(BytecodeError::InvalidSymbolTableKey(1)), DecodedProgram::new(bytecode));
    }

    #[test]
    fn test_invalid_key() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 15);

        let str_1: [u8; 4] = [0x41, 0x45, 0x60, 0];
        bytecode.extend_from_slice(&[1, 0, 0, 0]);
        bytecode.extend_from_slice(&str_1);

        let str_2: [u8; 3] = [0x42, 0x43, 0];
        bytecode.extend_from_slice(&[5, 0, 0, 0]);
        bytecode.extend_from_slice(&str_2);

        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);
        assert_eq!(Err(BytecodeError::InvalidSymbolTableKey(5)), DecodedProgram::new(bytecode));
    }

    #[test]
    fn test_cutoff_symbol_table_key() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 9);

        let str_1: [u8; 3] = [0x41, 0x42, 0];
        bytecode.extend_from_slice(&[1, 0, 0, 0]);
        bytecode.extend_from_slice(&str_1);

        bytecode.extend_from_slice(&[2, 0]);
        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);

        assert_eq!(Err(BytecodeError::UnexpectedEnd), DecodedProgram::new(bytecode));
    }

    #[test]
    fn test_incorrect_inst_size() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 0);
        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);
        bytecode.push(0x30);
        assert_eq!(Err(BytecodeError::IncorrectSectionSize), DecodedProgram::new(bytecode));
    }

    #[test]
    fn test_minimum_size_bytecode() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 0);
        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);
        assert_eq!(
            DecodedProgram { symbol_table: HashMap::new(), instructions: vec![] },
            DecodedProgram::new(bytecode).unwrap()
        );
    }

    #[test]
    fn test_incorrect_size_symbol_table() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, u32::MAX);
        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, 0);
        assert_eq!(Err(BytecodeError::IncorrectSectionSize), DecodedProgram::new(bytecode));
    }

    #[test]
    fn test_instructions() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 0);

        let instructions = [0x30, 0x01, 0x01, 0, 0, 0, 0x05, 0x10, 0, 0, 0x10];
        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, instructions.len() as u32);
        bytecode.extend_from_slice(&instructions);

        let program = DecodedProgram::new(bytecode).unwrap();
        assert_eq!(instructions.to_vec(), program.instructions);
    }

    #[test]
    fn test_invalid_instructions() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 0);

        let instructions = [0x30, 0x01, 0x01, 0, 0, 0, 0x05, 0x10, 0];
        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, instructions.len() as u32);
        bytecode.extend_from_slice(&instructions);

        // DecodedProgram does not validate the instruction bytecode, so it would still store it.
        // The instruction bytecode is validated when it's evaluated by a matcher.
        let program = DecodedProgram::new(bytecode).unwrap();
        assert_eq!(instructions.to_vec(), program.instructions);
    }

    #[test]
    fn test_valid_bytecode() {
        let mut bytecode: Vec<u8> = BIND_HEADER.to_vec();
        append_section_header(&mut bytecode, SYMB_MAGIC_NUM, 18);

        let str_1: [u8; 5] = [0x57, 0x52, 0x45, 0x4E, 0]; // "WREN"
        bytecode.extend_from_slice(&[1, 0, 0, 0]);
        bytecode.extend_from_slice(&str_1);

        let str_2: [u8; 5] = [0x44, 0x55, 0x43, 0x4B, 0]; // "DUCK"
        bytecode.extend_from_slice(&[2, 0, 0, 0]);
        bytecode.extend_from_slice(&str_2);

        let instructions = [0x30, 0x01, 0x01, 0, 0, 0, 0x05, 0x10, 0, 0, 0x10];
        append_section_header(&mut bytecode, INSTRUCTION_MAGIC_NUM, instructions.len() as u32);
        bytecode.extend_from_slice(&instructions);

        let mut expected_symbol_table: HashMap<u32, String> = HashMap::new();
        expected_symbol_table.insert(1, "WREN".to_string());
        expected_symbol_table.insert(2, "DUCK".to_string());

        assert_eq!(
            DecodedProgram {
                symbol_table: expected_symbol_table,
                instructions: instructions.to_vec()
            },
            DecodedProgram::new(bytecode).unwrap()
        );
    }
}
