// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod tests {
    // [START import]
    use fidl_fuchsia_examples as fex;
    // [END import]

    // [START bits]
    #[test]
    fn bits() {
        let flags = fex::FileMode::Read | fex::FileMode::Write;
        println!("{:?}", flags);
    }
    // [END bits]

    // [START enums]
    #[test]
    fn enums() {
        let from_raw = fex::LocationType::from_primitive(1).expect("Could not create LocationType");
        assert_eq!(from_raw, fex::LocationType::Museum);
        assert_eq!(fex::LocationType::Restaurant.into_primitive(), 3);
    }
    // [END enums]

    // [START structs]
    #[test]
    fn structs() {
        let red = fex::Color { id: 0u32, name: "red".to_string() };
        println!("{:?}", red);
    }
    // [END structs]

    // [START unions]
    #[test]
    fn unions() {
        let int_val = fex::JsonValue::IntValue(1);
        let str_val = fex::JsonValue::StringValue("1".to_string());
        println!("{:?}", int_val);
        assert_ne!(int_val, str_val);
    }
    // [END unions]

    // [START tables]
    #[test]
    fn tables() {
        let user = fex::User { age: Some(20), ..fex::User::empty() };
        println!("{:?}", user);
        assert!(user.age.is_some());
    }
    // [END tables]
}
