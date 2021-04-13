// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bind::compiler::Symbol;
use bind::match_bind::{match_bytecode, PropertyKey};
use std::collections::HashMap;

const BIND_PROTOCOL: u64 = 0x0001;
const BIND_AUTOBIND: u64 = 0x0002;

#[repr(C)]
pub struct device_property_t {
    key: u32,
    value: u32,
}

// |bytecode_sz| must match the size of |bytecode_c|. |properties_sz| must
// match the size o |properties_c|.
#[no_mangle]
pub extern "C" fn match_bind_rules(
    bytecode_c: *const u8,
    bytecode_sz: libc::size_t,
    properties_c: *const device_property_t,
    properties_sz: libc::size_t,
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

    let bytecode = unsafe { std::slice::from_raw_parts(bytecode_c, bytecode_sz).to_vec() };

    // TODO(fxb/73943): Return the error instead of returning false.
    match_bytecode(bytecode, device_properties).unwrap_or_else(|e| {
        println!("Error evaluating the bytecode: {}", e);
        false
    })
}
