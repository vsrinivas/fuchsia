// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::bytecode_constants::*;
use crate::compiler::Symbol;
use crate::interpreter::common::*;
use crate::interpreter::decode_bind_rules::DecodedBindRules;
use crate::parser::bind_library;
use core::hash::Hash;
use num_traits::FromPrimitive;
use std::collections::HashMap;

#[derive(PartialEq)]
enum Condition {
    Unconditional,
    Equal,
    Inequal,
}

// TODO(fxb/71834): Currently, the driver manager only supports number-based
// device properties. It will support string-based properties soon. We should
// support other device property types in the future.
#[derive(Clone, Hash, Eq, PartialEq)]
pub enum PropertyKey {
    NumberKey(u64),
    StringKey(String),
}

pub type DeviceProperties = HashMap<PropertyKey, Symbol>;

pub struct MatchBindData<'a> {
    pub symbol_table: &'a HashMap<u32, String>,
    pub instructions: &'a Vec<u8>,
}

struct DeviceMatcher<'a> {
    properties: &'a DeviceProperties,
    symbol_table: &'a HashMap<u32, String>,
    iter: BytecodeIter<'a>,
}

impl<'a> DeviceMatcher<'a> {
    pub fn match_bind(mut self) -> Result<bool, BytecodeError> {
        while let Some(byte) = self.iter.next() {
            let op_byte = FromPrimitive::from_u8(*byte).ok_or(BytecodeError::InvalidOp(*byte))?;
            match op_byte {
                RawOp::EqualCondition | RawOp::InequalCondition => {
                    if !self.evaluate_condition_inst(op_byte)? {
                        return Ok(false);
                    }
                }
                RawOp::Abort => {
                    return Ok(false);
                }
                RawOp::UnconditionalJump => self.evaluate_jump_inst(Condition::Unconditional)?,
                RawOp::JumpIfEqual => self.evaluate_jump_inst(Condition::Equal)?,
                RawOp::JumpIfNotEqual => self.evaluate_jump_inst(Condition::Inequal)?,
                RawOp::JumpLandPad => {
                    // No-op.
                }
            };
        }

        Ok(true)
    }

    // Evaluates a conditional instruction and returns false if the condition failed.
    fn evaluate_condition_inst(&mut self, op: RawOp) -> Result<bool, BytecodeError> {
        let condition = match op {
            RawOp::EqualCondition => Condition::Equal,
            RawOp::InequalCondition => Condition::Inequal,
            _ => panic!(
                "evaluate_condition_inst() should only be called for Equal or Inequal instructions"
            ),
        };

        Ok(self.read_and_evaluate_values(condition)?)
    }

    fn evaluate_jump_inst(&mut self, condition: Condition) -> Result<(), BytecodeError> {
        let offset = next_u32(&mut self.iter)?;
        if condition != Condition::Unconditional && !self.read_and_evaluate_values(condition)? {
            return Ok(());
        }

        // Skip through the bytes by the amount in the offset.
        for _ in 0..offset {
            next_u8(&mut self.iter)?;
        }

        // Verify that the next instruction is a jump pad.
        if *next_u8(&mut self.iter)? != RawOp::JumpLandPad as u8 {
            return Err(BytecodeError::InvalidJumpLocation);
        }

        Ok(())
    }

    // Read in two values and evaluate them based on the given condition.
    fn read_and_evaluate_values(&mut self, condition: Condition) -> Result<bool, BytecodeError> {
        let property_key = match self.read_next_value()? {
            Symbol::NumberValue(key) => PropertyKey::NumberKey(key),
            Symbol::StringValue(key) => PropertyKey::StringKey(key),
            Symbol::Key(key, _) => PropertyKey::StringKey(key),
            _ => {
                return Err(BytecodeError::InvalidKeyType);
            }
        };

        let bind_value = self.read_next_value()?;
        match self.properties.get(&property_key) {
            None => Ok(condition == Condition::Inequal),
            Some(device_value) => compare_symbols(condition, device_value, &bind_value),
        }
    }

    // Read in the next u8 as the value type and the next u32 as the value. Convert the value
    // into a Symbol.
    fn read_next_value(&mut self) -> Result<Symbol, BytecodeError> {
        let value_type = *next_u8(&mut self.iter)?;
        let value_type = FromPrimitive::from_u8(value_type)
            .ok_or(BytecodeError::InvalidValueType(value_type))?;

        let value = next_u32(&mut self.iter)?;
        match value_type {
            RawValueType::NumberValue => Ok(Symbol::NumberValue(value as u64)),
            RawValueType::Key => {
                // The key's value type is a placeholder. The value type doesn't matter since
                // the only the key will be used for looking up the device property.
                Ok(Symbol::Key(self.lookup_symbol_table(value)?, bind_library::ValueType::Str))
            }
            RawValueType::StringValue => Ok(Symbol::StringValue(self.lookup_symbol_table(value)?)),
            RawValueType::BoolValue => match value {
                0x0 => Ok(Symbol::BoolValue(false)),
                0x1 => Ok(Symbol::BoolValue(true)),
                _ => Err(BytecodeError::InvalidBoolValue(value)),
            },
            RawValueType::EnumValue => Ok(Symbol::EnumValue(self.lookup_symbol_table(value)?)),
        }
    }

    fn lookup_symbol_table(&self, key: u32) -> Result<String, BytecodeError> {
        self.symbol_table
            .get(&key)
            .ok_or(BytecodeError::MissingEntryInSymbolTable(key))
            .map(|val| val.to_string())
    }
}

fn compare_symbols(
    condition: Condition,
    lhs: &Symbol,
    rhs: &Symbol,
) -> Result<bool, BytecodeError> {
    if std::mem::discriminant(lhs) != std::mem::discriminant(rhs) {
        return Err(BytecodeError::MismatchValueTypes);
    }

    Ok(match condition {
        Condition::Equal => lhs == rhs,
        Condition::Inequal => lhs != rhs,
        Condition::Unconditional => {
            panic!("This function shouldn't be called for Unconditional.")
        }
    })
}

// Return true if the bytecode matches the device properties. The bytecode
// is for a non-composite driver.
pub fn match_bytecode(
    bytecode: Vec<u8>,
    properties: &DeviceProperties,
) -> Result<bool, BytecodeError> {
    let decoded_bind_rules = DecodedBindRules::from_bytecode(bytecode)?;
    let matcher = DeviceMatcher {
        properties: &properties,
        symbol_table: &decoded_bind_rules.symbol_table,
        iter: decoded_bind_rules.instructions.iter(),
    };
    matcher.match_bind()
}

// Return true if the bind rules matches the device properties.
pub fn match_bind(
    bind_data: MatchBindData,
    properties: &DeviceProperties,
) -> Result<bool, BytecodeError> {
    let matcher = DeviceMatcher {
        properties: &properties,
        symbol_table: &bind_data.symbol_table,
        iter: bind_data.instructions.iter(),
    };
    matcher.match_bind()
}

#[cfg(test)]
mod test {
    use super::*;

    // Constants representing the number of bytes in an operand and value.
    const OP_BYTES: u32 = 1;
    const VALUE_BYTES: u32 = 5;
    const OFFSET_BYTES: u32 = 4;

    // Constants representing the number of bytes in each instruction.
    const ABORT_BYTES: u32 = OP_BYTES;
    const COND_INST_BYTES: u32 = OP_BYTES + VALUE_BYTES + VALUE_BYTES;
    const UNCOND_JMP_BYTES: u32 = OP_BYTES + OFFSET_BYTES;
    const COND_JMP_BYTES: u32 = OP_BYTES + OFFSET_BYTES + VALUE_BYTES + VALUE_BYTES;
    const JMP_PAD_BYTES: u32 = OP_BYTES;

    struct EncodedValue {
        value_type: RawValueType,
        value: u32,
    }

    fn append_encoded_value(bytecode: &mut Vec<u8>, encoded_val: EncodedValue) {
        bytecode.push(encoded_val.value_type as u8);
        bytecode.extend_from_slice(&encoded_val.value.to_le_bytes());
    }

    fn append_abort(bytecode: &mut Vec<u8>) {
        bytecode.push(0x30);
    }

    fn append_equal_cond(
        bytecode: &mut Vec<u8>,
        property_key: EncodedValue,
        property_value: EncodedValue,
    ) {
        bytecode.push(0x01);
        append_encoded_value(bytecode, property_key);
        append_encoded_value(bytecode, property_value);
    }

    fn append_inequal_cond(
        bytecode: &mut Vec<u8>,
        property_key: EncodedValue,
        property_value: EncodedValue,
    ) {
        bytecode.push(0x02);
        append_encoded_value(bytecode, property_key);
        append_encoded_value(bytecode, property_value);
    }

    fn append_unconditional_jump(bytecode: &mut Vec<u8>, offset: u32) {
        bytecode.push(0x10);
        bytecode.extend_from_slice(&offset.to_le_bytes());
    }

    fn append_jump_if_equal(
        bytecode: &mut Vec<u8>,
        offset: u32,
        property_key: EncodedValue,
        property_value: EncodedValue,
    ) {
        bytecode.push(0x11);
        bytecode.extend_from_slice(&offset.to_le_bytes());
        append_encoded_value(bytecode, property_key);
        append_encoded_value(bytecode, property_value);
    }

    fn append_jump_if_not_equal(
        bytecode: &mut Vec<u8>,
        offset: u32,
        property_key: EncodedValue,
        property_value: EncodedValue,
    ) {
        bytecode.push(0x12);
        bytecode.extend_from_slice(&offset.to_le_bytes());
        append_encoded_value(bytecode, property_key);
        append_encoded_value(bytecode, property_value);
    }

    fn append_jump_pad(bytecode: &mut Vec<u8>) {
        bytecode.push(0x20);
    }

    fn verify_match_result(
        expected_result: Result<bool, BytecodeError>,
        bind_rules: DecodedBindRules,
        device_properties: &DeviceProperties,
    ) {
        let matcher = DeviceMatcher {
            properties: device_properties,
            symbol_table: &bind_rules.symbol_table,
            iter: bind_rules.instructions.iter(),
        };

        assert_eq!(expected_result, matcher.match_bind());
    }

    #[test]
    fn empty_instructions() {
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: vec![] },
            &HashMap::new(),
        );
    }

    #[test]
    fn equal_condition_with_number_property_keys() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(1), Symbol::NumberValue(2000));
        device_properties.insert(PropertyKey::NumberKey(2), Symbol::NumberValue(500));

        // The condition statement should match the device properties.
        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2000 },
        );
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );

        // The condition should fail since the device property has a different value.
        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
        );
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );

        // The condition should fail since the property is missing from device properties.
        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 3 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
        );
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn equal_condition_with_string_property_keys() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(
            PropertyKey::StringKey("nightjar".to_string()),
            Symbol::StringValue("poorwill".to_string()),
        );

        let mut symbol_table: HashMap<u32, String> = HashMap::new();
        symbol_table.insert(1, "nightjar".to_string());
        symbol_table.insert(2, "poorwill".to_string());
        symbol_table.insert(3, "nighthawk".to_string());

        // The condition statement should match the string device properties.
        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: symbol_table.clone(), instructions: instructions },
            &device_properties,
        );

        // The condition should fail since the string device property has a different value.
        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
            EncodedValue { value_type: RawValueType::StringValue, value: 3 },
        );
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: symbol_table.clone(), instructions: instructions },
            &device_properties,
        );

        // The condition should fail since the property is missing from string device properties.
        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
        );
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: symbol_table.clone(), instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn inequal_condition_with_number_property_keys() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(1), Symbol::NumberValue(2000));
        device_properties.insert(PropertyKey::NumberKey(2), Symbol::NumberValue(500));

        // The condition should match since the device property has a different value.
        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 500 },
        );
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );

        // The condition should match since the device properties doesn't contain the property.
        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
        );
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );

        // The condition should fail since the device properties matches the value.
        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 2 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 500 },
        );
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn inequal_condition_with_string_property_keys() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(
            PropertyKey::StringKey("nightjar".to_string()),
            Symbol::StringValue("poorwill".to_string()),
        );

        let mut symbol_table: HashMap<u32, String> = HashMap::new();
        symbol_table.insert(1, "nightjar".to_string());
        symbol_table.insert(2, "poorwill".to_string());

        // The condition should match since the string device property has a different value.
        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
        );
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: symbol_table.clone(), instructions: instructions },
            &device_properties,
        );

        // The condition should match since the string device properties doesn't contain the
        // property.
        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
        );
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: symbol_table.clone(), instructions: instructions },
            &device_properties,
        );

        // The condition should fail since the string device properties matches the value.
        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: symbol_table.clone(), instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn unconditional_abort() {
        let mut instructions: Vec<u8> = vec![];
        append_abort(&mut instructions);
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &HashMap::new(),
        );
    }

    #[test]
    fn match_with_key_values() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties
            .insert(PropertyKey::StringKey("timberdoodle".to_string()), Symbol::NumberValue(2000));

        let mut symbol_table: HashMap<u32, String> = HashMap::new();
        symbol_table.insert(1, "timberdoodle".to_string());

        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::Key, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2000 },
        );
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: symbol_table.clone(), instructions: instructions },
            &device_properties,
        );

        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::Key, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 500 },
        );
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: symbol_table, instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn match_with_bool_values() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(1), Symbol::BoolValue(true));

        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 1 },
        );
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );

        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 0 },
        );
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn missing_entry_in_symbol_table() {
        let mut symbol_table: HashMap<u32, String> = HashMap::new();
        symbol_table.insert(1, "nightjar".to_string());
        symbol_table.insert(2, "poorwill".to_string());

        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 10 },
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
        );
        verify_match_result(
            Err(BytecodeError::MissingEntryInSymbolTable(10)),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &HashMap::new(),
        );

        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::Key, value: 15 },
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
        );
        verify_match_result(
            Err(BytecodeError::MissingEntryInSymbolTable(15)),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &HashMap::new(),
        );
    }

    #[test]
    fn invalid_op() {
        let mut instructions: Vec<u8> = vec![0xFF];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
        );

        verify_match_result(
            Err(BytecodeError::InvalidOp(0xFF)),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &HashMap::new(),
        );
    }

    #[test]
    fn invalid_value_type() {
        let instructions: Vec<u8> = vec![0x01, 0x05, 0, 0, 0, 0, 0x01, 0, 0, 0, 0];
        verify_match_result(
            Err(BytecodeError::InvalidValueType(0x05)),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &HashMap::new(),
        );
    }

    #[test]
    fn invalid_bool_value() {
        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 15 },
        );

        verify_match_result(
            Err(BytecodeError::InvalidBoolValue(15)),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &HashMap::new(),
        );
    }

    #[test]
    fn invalid_key_type() {
        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::BoolValue, value: 1 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 15 },
        );

        verify_match_result(
            Err(BytecodeError::InvalidKeyType),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &HashMap::new(),
        );
    }

    #[test]
    fn mismatch_value_types() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(1), Symbol::NumberValue(2000));
        device_properties.insert(
            PropertyKey::StringKey("tyrant".to_string()),
            Symbol::StringValue("flycatcher".to_string()),
        );

        let mut symbol_table: HashMap<u32, String> = HashMap::new();
        symbol_table.insert(1, "tyrant".to_string());
        symbol_table.insert(2, "flycatcher".to_string());

        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 15 },
        );

        verify_match_result(
            Err(BytecodeError::MismatchValueTypes),
            DecodedBindRules { symbol_table: symbol_table, instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn invalid_condition_statement() {
        let instructions: Vec<u8> = vec![0x01, 0x02, 0, 0, 0];
        verify_match_result(
            Err(BytecodeError::UnexpectedEnd),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &HashMap::new(),
        );
    }

    #[test]
    fn match_with_multiple_condition_statements() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(10), Symbol::NumberValue(2000));
        device_properties.insert(PropertyKey::NumberKey(2), Symbol::NumberValue(500));
        device_properties.insert(
            PropertyKey::StringKey("rail".to_string()),
            Symbol::StringValue("crake".to_string()),
        );

        let mut symbol_table: HashMap<u32, String> = HashMap::new();
        symbol_table.insert(1, "crake".to_string());
        symbol_table.insert(2, "rail".to_string());

        let mut instructions: Vec<u8> = vec![];
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 200 },
        );
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2000 },
        );

        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: symbol_table, instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn no_match_with_multiple_condition_statements() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(10), Symbol::NumberValue(2000));
        device_properties.insert(PropertyKey::NumberKey(2), Symbol::NumberValue(500));
        device_properties.insert(
            PropertyKey::StringKey("rail".to_string()),
            Symbol::StringValue("crake".to_string()),
        );

        let mut symbol_table: HashMap<u32, String> = HashMap::new();
        symbol_table.insert(1, "crake".to_string());
        symbol_table.insert(2, "rail".to_string());

        let mut instructions: Vec<u8> = vec![];
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
        );
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 2 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 5000 },
        );
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 40 },
        );

        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: symbol_table, instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn unconditional_jump() {
        let mut instructions: Vec<u8> = vec![];
        append_unconditional_jump(&mut instructions, ABORT_BYTES + COND_INST_BYTES);
        append_abort(&mut instructions);
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2000 },
        );
        append_jump_pad(&mut instructions);

        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &HashMap::new(),
        );
    }

    #[test]
    fn jump_if_equal() {
        let mut symbol_table: HashMap<u32, String> = HashMap::new();
        symbol_table.insert(1, "whimbrel".to_string());

        let mut instructions: Vec<u8> = vec![];
        append_jump_if_equal(
            &mut instructions,
            ABORT_BYTES * 2,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
        );
        append_abort(&mut instructions);
        append_abort(&mut instructions);
        append_jump_pad(&mut instructions);

        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties
            .insert(PropertyKey::NumberKey(10), Symbol::StringValue("whimbrel".to_string()));
        verify_match_result(
            Ok(true),
            DecodedBindRules {
                symbol_table: symbol_table.clone(),
                instructions: instructions.clone(),
            },
            &device_properties,
        );

        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties
            .insert(PropertyKey::NumberKey(10), Symbol::StringValue("godwit".to_string()));
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: symbol_table.clone(), instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn jump_if_not_equal() {
        let mut instructions: Vec<u8> = vec![];
        append_jump_if_not_equal(
            &mut instructions,
            ABORT_BYTES * 2,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2000 },
        );
        append_abort(&mut instructions);
        append_abort(&mut instructions);
        append_jump_pad(&mut instructions);

        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(10), Symbol::NumberValue(2000));
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions.clone() },
            &device_properties,
        );

        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(10), Symbol::NumberValue(20));
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn no_jump_pad() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(10), Symbol::NumberValue(2000));

        let mut instructions: Vec<u8> = vec![];
        append_jump_if_equal(
            &mut instructions,
            ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2000 },
        );
        append_abort(&mut instructions);
        append_abort(&mut instructions);
        verify_match_result(
            Err(BytecodeError::InvalidJumpLocation),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn jump_out_of_bounds() {
        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(10), Symbol::NumberValue(2000));

        let mut instructions: Vec<u8> = vec![];
        append_jump_if_equal(
            &mut instructions,
            ABORT_BYTES + JMP_PAD_BYTES + 1,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2000 },
        );
        append_abort(&mut instructions);
        append_jump_pad(&mut instructions);
        verify_match_result(
            Err(BytecodeError::UnexpectedEnd),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn nested_jump_instructions() {
        let mut instructions: Vec<u8> = vec![];
        append_jump_if_equal(
            &mut instructions,
            ABORT_BYTES * 2 + UNCOND_JMP_BYTES + JMP_PAD_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 10 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2000 },
        );
        append_unconditional_jump(&mut instructions, ABORT_BYTES);
        append_abort(&mut instructions);
        append_jump_pad(&mut instructions);
        append_abort(&mut instructions);
        append_jump_pad(&mut instructions);

        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(10), Symbol::NumberValue(2000));
        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions.clone() },
            &device_properties,
        );

        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(PropertyKey::NumberKey(10), Symbol::NumberValue(200));
        device_properties.insert(
            PropertyKey::StringKey("kingfisher".to_string()),
            Symbol::StringValue("kookaburra".to_string()),
        );
        verify_match_result(
            Ok(false),
            DecodedBindRules { symbol_table: HashMap::new(), instructions: instructions },
            &device_properties,
        );
    }

    #[test]
    fn complex_mix_of_bind_rules() {
        let mut symbol_table: HashMap<u32, String> = HashMap::new();
        symbol_table.insert(1, "kingfisher".to_string());
        symbol_table.insert(2, "kookaburra".to_string());

        let mut instructions: Vec<u8> = vec![];
        append_jump_if_equal(
            &mut instructions,
            COND_INST_BYTES + ABORT_BYTES,
            EncodedValue { value_type: RawValueType::NumberValue, value: 5 },
            EncodedValue { value_type: RawValueType::BoolValue, value: 1 },
        );
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 2 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 2000 },
        );
        append_abort(&mut instructions);
        append_jump_pad(&mut instructions);
        append_unconditional_jump(&mut instructions, COND_JMP_BYTES + ABORT_BYTES);
        append_jump_if_not_equal(
            &mut instructions,
            ABORT_BYTES + JMP_PAD_BYTES + COND_INST_BYTES,
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );
        append_abort(&mut instructions);
        append_jump_pad(&mut instructions);
        append_inequal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::NumberValue, value: 1 },
            EncodedValue { value_type: RawValueType::NumberValue, value: 50 },
        );
        append_jump_pad(&mut instructions);
        append_equal_cond(
            &mut instructions,
            EncodedValue { value_type: RawValueType::StringValue, value: 1 },
            EncodedValue { value_type: RawValueType::StringValue, value: 2 },
        );

        let mut device_properties: DeviceProperties = HashMap::new();
        device_properties.insert(
            PropertyKey::StringKey("kingfisher".to_string()),
            Symbol::StringValue("kookaburra".to_string()),
        );
        device_properties.insert(PropertyKey::NumberKey(1), Symbol::NumberValue(9));
        device_properties.insert(PropertyKey::NumberKey(2), Symbol::NumberValue(100));
        device_properties.insert(PropertyKey::NumberKey(5), Symbol::BoolValue(true));

        verify_match_result(
            Ok(true),
            DecodedBindRules { symbol_table: symbol_table, instructions: instructions },
            &device_properties,
        );
    }
}
