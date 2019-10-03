// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::ast, heck::SnakeCase, std::collections::HashMap, std::iter};

pub fn to_c_name(name: &str) -> String {
    if name.is_empty() {
        name.to_string()
    } else {
        // strip FQN
        let name = name.split(".").last().unwrap();
        // Force uppercase characters the follow one another to lowercase.
        // e.g. GUIDType becomes Guidtype
        let mut iter = name.chars();
        let name = iter::once(iter.next().unwrap())
            .chain(iter.zip(name.chars()).map(|(c1, c2)| {
                if c2.is_ascii_uppercase() {
                    c1.to_ascii_lowercase()
                } else {
                    c1
                }
            }))
            .collect::<String>();
        name.trim().to_snake_case()
    }
}

/// Represents attributes on a method of a particular type, where the attribute
/// values are formatted as pairs. For example, with an annotation like this:
/// ```
/// [zippy="p0 ABC",
///  zippy="p1 XYZ",
///  something="maybe",
///  other]
/// ```
/// a `ValuedAttributes` for attr_key="zippy", would be {p0: ABC, p1: XYZ}.
#[derive(Default)]
pub struct ValuedAttributes(HashMap<String, String>);

impl ValuedAttributes {
    pub fn new(attrs: &ast::Attrs, attr_key: &str) -> ValuedAttributes {
        let mut result: HashMap<String, String> = HashMap::new();
        for attr in attrs.0.iter() {
            if attr.key == attr_key {
                if let Some(ref val) = attr.val {
                    let parts: Vec<&str> = val.split(' ').collect();
                    if parts.len() != 2 {
                        panic!("argype annotation requires a \"name ANNOT\" value");
                    }
                    result.insert(parts[0].to_string(), parts[1].to_string());
                } else {
                    panic!("argype annotation requires a string key");
                }
            }
        }
        ValuedAttributes(result)
    }

    // Returns the value for a given name, prepended with prefix.
    // Note that this name is the LHS of the string value, not the outer
    // attribute name. In the example above, the name would be "p0" or "p1",
    // rather than "zippy".
    pub fn get_arg_with_prefix(&self, name: &str, prefix: &str) -> String {
        match self.0.get(name) {
            Some(annot) => prefix.to_owned() + &annot,
            _ => String::default(),
        }
    }

    pub fn get_arg(&self, name: &str) -> String {
        self.get_arg_with_prefix(name, "")
    }
}
