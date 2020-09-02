// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::Deserialize;

#[derive(Deserialize, Debug, Clone, PartialEq)]
pub struct VariableName {
    pub name: String,
}

impl<'a> VariableName {
    pub fn new(name: String) -> VariableName {
        VariableName { name }
    }

    pub fn original_name(&'a self) -> &'a str {
        &self.name
    }

    pub fn includes_namespace(&self) -> bool {
        self.name.contains(':')
    }

    pub fn name_parts(&'a self, namespace: &'a str) -> Option<(&'a str, &'a str)> {
        let name_parts = self.name.split("::").collect::<Vec<_>>();
        match name_parts.len() {
            1 => Some((namespace, &self.name)),
            2 => {
                let namespace_length = name_parts[0].len();
                let name_start = namespace_length + 2;
                let name_end = name_start + name_parts[1].len();
                Some((&self.name[..namespace_length], &self.name[name_start..name_end]))
            }
            _ => None,
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn name_with_namespace() {
        let name = VariableName::new("a::b".to_string());
        assert_eq!(name.original_name(), "a::b".to_string());
        assert_eq!(name.includes_namespace(), true);
        assert_eq!(name.name_parts("c"), Some(("a", "b")));
    }

    #[test]
    fn name_without_namespace() {
        let name = VariableName::new("b".to_string());
        assert_eq!(name.original_name(), "b".to_string());
        assert_eq!(name.includes_namespace(), false);
        assert_eq!(name.name_parts("c"), Some(("c", "b")));
    }

    #[test]
    fn too_many_namespaces() {
        let name = VariableName::new("a::b::c".to_string());
        assert_eq!(name.original_name(), "a::b::c".to_string());
        assert_eq!(name.includes_namespace(), true);
        assert_eq!(name.name_parts("d"), None);
    }
}
