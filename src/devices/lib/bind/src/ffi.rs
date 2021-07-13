// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bind::compiler::Symbol;
use bind::interpreter::match_bind::{match_bytecode, PropertyKey};
use std::collections::HashMap;
use std::ffi::CStr;

const BIND_PROTOCOL: u64 = 0x0001;
const BIND_AUTOBIND: u64 = 0x0002;

#[repr(u32)]
enum ValueType {
    NumberVal = 0,
    StringVal = 1,
    BoolVal = 2,
}

#[repr(C)]
pub struct device_property_t {
    key: u32,
    value: u32,
}

#[repr(C)]
pub union value_t {
    num_value: u32,
    str_value: *const libc::c_char,
    bool_value: bool,
}

#[repr(C)]
struct property_value_t {
    val_type: ValueType,
    value: value_t,
}

#[repr(C)]
pub struct device_str_property_t {
    key: *const libc::c_char,
    value: property_value_t,
}

fn convert_str(c_str: *const libc::c_char) -> Option<String> {
    let str = unsafe { CStr::from_ptr(c_str) };
    match str.to_str() {
        Ok(value) => Some(value.to_string()),
        Err(_) => None,
    }
}

fn convert_to_symbol(prop_value: &property_value_t) -> Option<Symbol> {
    unsafe {
        match prop_value {
            property_value_t {
                val_type: ValueType::NumberVal,
                value: value_t { num_value: val },
            } => Some(Symbol::NumberValue(*val as u64)),
            property_value_t {
                val_type: ValueType::BoolVal,
                value: value_t { bool_value: val },
            } => Some(Symbol::BoolValue(*val)),
            property_value_t {
                val_type: ValueType::StringVal,
                value: value_t { str_value: val },
            } => convert_str(*val).map(|str_val| Symbol::StringValue(str_val)),
        }
    }
}

#[no_mangle]
pub extern "C" fn str_property_with_string(
    key: *const libc::c_char,
    value: *const libc::c_char,
) -> device_str_property_t {
    device_str_property_t {
        key: key,
        value: property_value_t {
            val_type: ValueType::StringVal,
            value: value_t { str_value: value },
        },
    }
}

#[no_mangle]
pub extern "C" fn str_property_with_int(
    key: *const libc::c_char,
    value: u32,
) -> device_str_property_t {
    device_str_property_t {
        key: key,
        value: property_value_t {
            val_type: ValueType::NumberVal,
            value: value_t { num_value: value },
        },
    }
}

#[no_mangle]
pub extern "C" fn str_property_with_bool(
    key: *const libc::c_char,
    value: bool,
) -> device_str_property_t {
    device_str_property_t {
        key: key,
        value: property_value_t {
            val_type: ValueType::BoolVal,
            value: value_t { bool_value: value },
        },
    }
}

// |bytecode_sz| must match the size of |bytecode_c|. |properties_sz| must
// match the size o |properties_c|.
#[no_mangle]
pub extern "C" fn match_bind_rules(
    bytecode_c: *const u8,
    bytecode_sz: libc::size_t,
    properties_c: *const device_property_t,
    properties_sz: libc::size_t,
    str_properties_c: *const device_str_property_t,
    str_properties_sz: libc::size_t,
    protocol_id: u32,
    autobind: bool,
) -> bool {
    if bytecode_c.is_null() {
        return false;
    }

    let mut device_properties = HashMap::new();
    if !properties_c.is_null() {
        let properties = unsafe { std::slice::from_raw_parts(properties_c, properties_sz) };
        for property in properties.iter() {
            device_properties.insert(
                PropertyKey::NumberKey(property.key as u64),
                Symbol::NumberValue(property.value as u64),
            );
        }
    }

    // Insert the protocol ID property if it's missing.
    device_properties
        .entry(PropertyKey::NumberKey(BIND_PROTOCOL))
        .or_insert(Symbol::NumberValue(protocol_id as u64));
    device_properties
        .entry(PropertyKey::NumberKey(BIND_AUTOBIND))
        .or_insert(Symbol::NumberValue(if autobind { 1 } else { 0 }));

    if !str_properties_c.is_null() {
        let str_properties =
            unsafe { std::slice::from_raw_parts(str_properties_c, str_properties_sz) };
        for str_property in str_properties.iter() {
            let key = convert_str(str_property.key);
            let symbol = convert_to_symbol(&str_property.value);

            assert!(key.is_some() && symbol.is_some());
            device_properties.insert(PropertyKey::StringKey(key.unwrap()), symbol.unwrap());
        }
    }

    let bytecode = unsafe { std::slice::from_raw_parts(bytecode_c, bytecode_sz).to_vec() };

    // TODO(fxb/73943): Return the error instead of returning false.
    match_bytecode(bytecode, &device_properties).unwrap_or_else(|e| {
        println!("Error evaluating the bytecode: {}", e);
        false
    })
}
