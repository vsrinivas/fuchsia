// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// [START import]
use fidl_fuchsia_examples as fex;
// [END import]

// In documentation, we show example code for strict/flexible behavior on the
// same type, e.g. "If LocationType is flexible, then ...". This module allows
// us to write code that looks like it's referring to fex::LocationType, but is
// actually fex::FlexibleLocationType.
mod flexible_fex {
    pub use fidl_fuchsia_examples::{
        FlexibleJsonValue as JsonValue, FlexibleJsonValueUnknown as JsonValueUnknown,
        FlexibleLocationType as LocationType, FlexibleLocationTypeUnknown as LocationTypeUnknown,
    };
}

fn main() {
    // [START bits]
    let flags = fex::FileMode::Read | fex::FileMode::Write;
    println!("{:?}", flags);
    // [END bits]

    // [START enums_init]
    let from_raw = fex::LocationType::from_primitive(1).expect("Could not create LocationType");
    assert_eq!(from_raw, fex::LocationType::Museum);
    assert_eq!(fex::LocationType::Restaurant.into_primitive(), 3);
    // [END enums_init]

    {
        use flexible_fex as fex;
        let location_type = fex::LocationType::Museum;
        // [START enums_flexible_match]
        match location_type {
            fex::LocationType::Museum => println!("museum"),
            fex::LocationType::Airport => println!("airport"),
            fex::LocationType::Restaurant => println!("restaurant"),
            fex::LocationTypeUnknown!() => println!("unknown!"),
        }
        // [END enums_flexible_match]
    }

    // [START structs]
    let red = fex::Color { id: 0u32, name: "red".to_string() };
    println!("{:?}", red);
    // [END structs]

    // [START unions_init]
    let int_val = fex::JsonValue::IntValue(1);
    let str_val = fex::JsonValue::StringValue("1".to_string());
    println!("{:?}", int_val);
    assert_ne!(int_val, str_val);
    // [END unions_init]

    {
        use flexible_fex as fex;
        let json_value = fex::JsonValue::IntValue(1);
        // [START unions_flexible_match]
        match json_value {
            fex::JsonValue::IntValue(val) => println!("int: {}", val),
            fex::JsonValue::StringValue(val) => println!("string: {}", val),
            fex::JsonValueUnknown!() => println!("unknown!"),
        }
        // [END unions_flexible_match]
    }

    // [START tables_init]
    let user = fex::User { age: Some(20), ..fex::User::empty() };
    println!("{:?}", user);
    assert!(user.age.is_some());
    // [END tables_init]

    #[allow(unused_variables)]
    // [START tables_match]
    let fex::User { age, name, .. } = user;
    // [END tables_match]
}
