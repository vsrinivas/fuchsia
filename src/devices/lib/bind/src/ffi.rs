// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bind::compiler::Symbol;
use bind::match_bind::{match_bytecode, PropertyKey};
use std::collections::HashMap;

#[repr(C)]
pub struct device_property_t {
    key: u32,
    value: u32,
}

#[no_mangle]
pub unsafe extern "C" fn match_bind_rules(
    bytecode_sz: libc::size_t,
    bytecode_c: *const u8,
    properties_sz: libc::size_t,
    properties_c: *const device_property_t,
) -> bool {
    if bytecode_c.is_null() {
        return false;
    }

    let bytecode = std::slice::from_raw_parts(bytecode_c, bytecode_sz).to_vec();

    let mut device_properties = HashMap::new();
    if !properties_c.is_null() {
        let properties = std::slice::from_raw_parts(properties_c, properties_sz);
        for property in properties.iter() {
            device_properties.insert(
                PropertyKey::NumberKey(property.key as u64),
                Symbol::NumberValue(property.value as u64),
            );
        }
    }

    // TODO(fxb/73943): Return the error instead of returning false.
    match_bytecode(bytecode, device_properties).unwrap_or_else(|e| {
        println!("Error evaluating the bytecode: {}", e);
        false
    })
}
