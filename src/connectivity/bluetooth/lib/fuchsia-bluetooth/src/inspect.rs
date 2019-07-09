// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

const FALSE_VALUE: u64 = 0;
const TRUE_VALUE: u64 = 1;

/// Convert a type to the correct supported Inspect Property type. This is used in Bluetooth to
/// ensure consistent representations of values in Inspect.
///
/// Note: It represents them appropriately for Bluetooth but may not be the appropriate type
/// for other use cases. It shouldn't be used outside of the Bluetooth project.
pub trait ToProperty {
    type PropertyType;
    fn to_property(self) -> Self::PropertyType;
}

impl ToProperty for bool {
    type PropertyType = u64;
    fn to_property(self) -> Self::PropertyType {
        if self {
            TRUE_VALUE
        } else {
            FALSE_VALUE
        }
    }
}

impl ToProperty for Option<bool> {
    type PropertyType = u64;
    fn to_property(self) -> Self::PropertyType {
        self.map(bool::to_property).unwrap_or(FALSE_VALUE)
    }
}

/// Vectors of Strings show up as a comma separated list string property
impl ToProperty for &Vec<String> {
    type PropertyType = String;
    fn to_property(self) -> Self::PropertyType {
        self.join(", ")
    }
}

/// Vectors of Strings show up as a comma separated list string property. `None` types are
/// represented as an empty string.
impl ToProperty for &Option<Vec<String>> {
    type PropertyType = String;
    fn to_property(self) -> Self::PropertyType {
        self.as_ref().map(ToProperty::to_property).unwrap_or_else(String::new)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bool_to_property() {
        let b = false.to_property();
        assert_eq!(b, FALSE_VALUE);
        let b = true.to_property();
        assert_eq!(b, TRUE_VALUE);
    }

    #[test]
    fn optional_bool_to_property() {
        let b = None.to_property();
        assert_eq!(b, FALSE_VALUE);
        let b = Some(false).to_property();
        assert_eq!(b, FALSE_VALUE);
        let b = Some(true).to_property();
        assert_eq!(b, TRUE_VALUE);
    }

    #[test]
    fn string_vec_to_property() {
        let s = vec![].to_property();
        assert_eq!(s, "");
        let s = vec!["foo".to_string()].to_property();
        assert_eq!(s, "foo");
        let s = vec!["foo".to_string(), "bar".to_string(), "baz".to_string()].to_property();
        assert_eq!(s, "foo, bar, baz");
    }

    #[test]
    fn optional_string_vec_to_property() {
        let s = Some(vec!["foo".to_string(), "bar".to_string(), "baz".to_string()]).to_property();
        assert_eq!(s, "foo, bar, baz");
    }
}
