// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use heck::SnakeCase;
use serde::Deserialize;
use serde::Serialize;
use serde_json::Map;
use serde_json::Value;

use std::collections::HashMap;
use std::fs::File;
use std::io;
use std::io::prelude::*;
use std::path::PathBuf;

/// Converts an UpperCamelCased name like "FooBar" into a lower_snake_cased one
/// like "foo_bar."  This is used to normalize attribute names such that names
/// written in either case are synonyms.
pub fn to_lower_snake_case(str: &str) -> String {
    str.to_snake_case().to_lowercase()
}

#[derive(Serialize, Deserialize)]
pub struct TableOfContentsItem {
    pub name: String,
    pub link: String,
    pub description: String,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct FidlJson {
    pub name: String,
    #[serde(default)]
    pub maybe_attributes: Vec<Value>,
    pub library_dependencies: Vec<Value>,
    pub bits_declarations: Vec<Value>,
    pub const_declarations: Vec<Value>,
    pub enum_declarations: Vec<Value>,
    pub interface_declarations: Vec<Value>,
    pub table_declarations: Vec<Value>,
    pub type_alias_declarations: Vec<Value>,
    pub struct_declarations: Vec<Value>,
    pub union_declarations: Vec<Value>,
    pub declaration_order: Vec<String>,
    pub declarations: Map<String, Value>,
}

impl FidlJson {
    pub fn from_path(path: &PathBuf) -> Result<FidlJson, io::Error> {
        let mut fidl_file = match File::open(path) {
            Err(why) => {
                eprintln!(
                    "Couldn't open file {path}: {reason}",
                    path = path.display(),
                    reason = why,
                );
                return Err(why);
            }
            Ok(file) => file,
        };

        let mut s = String::new();
        fidl_file.read_to_string(&mut s)?;

        Ok(serde_json::from_str(&s)?)
    }

    pub fn sort_declarations(&mut self) {
        let cmp_name = |a: &Value, b: &Value| a["name"].as_str().cmp(&b["name"].as_str());
        let FidlJson {
            name: _,
            maybe_attributes: _,
            library_dependencies: _,
            bits_declarations,
            const_declarations,
            enum_declarations,
            interface_declarations,
            table_declarations,
            type_alias_declarations,
            struct_declarations,
            union_declarations,
            declaration_order: _,
            declarations: _,
        } = self;
        bits_declarations.sort_unstable_by(cmp_name);
        const_declarations.sort_unstable_by(cmp_name);
        enum_declarations.sort_unstable_by(cmp_name);
        interface_declarations.sort_unstable_by(cmp_name);
        for interface in interface_declarations.iter_mut() {
            interface["methods"].as_array_mut().unwrap().sort_unstable_by(cmp_name);
        }
        table_declarations.sort_unstable_by(cmp_name);
        type_alias_declarations.sort_unstable_by(cmp_name);
        struct_declarations.sort_unstable_by(cmp_name);
        union_declarations.sort_unstable_by(cmp_name);
    }
}

pub struct FidlJsonPackageData {
    pub declarations: Vec<String>,
    pub fidl_json_map: HashMap<String, FidlJson>,
}

impl FidlJsonPackageData {
    pub fn new() -> Self {
        FidlJsonPackageData { declarations: Vec::new(), fidl_json_map: HashMap::new() }
    }

    pub fn insert(&mut self, mut fidl_json: FidlJson) {
        self.declarations.append(&mut fidl_json.declaration_order);
        let package_name = fidl_json.name.clone();
        self.fidl_json_map
            .entry(package_name)
            .and_modify(|package_fidl_json| {
                // Merge
                package_fidl_json.maybe_attributes.append(&mut fidl_json.maybe_attributes);
                package_fidl_json.bits_declarations.append(&mut fidl_json.bits_declarations);
                package_fidl_json.const_declarations.append(&mut fidl_json.const_declarations);
                package_fidl_json.enum_declarations.append(&mut fidl_json.enum_declarations);
                package_fidl_json
                    .interface_declarations
                    .append(&mut fidl_json.interface_declarations);
                package_fidl_json.struct_declarations.append(&mut fidl_json.struct_declarations);
                package_fidl_json.table_declarations.append(&mut fidl_json.table_declarations);
                package_fidl_json
                    .type_alias_declarations
                    .append(&mut fidl_json.type_alias_declarations);
                package_fidl_json.union_declarations.append(&mut fidl_json.union_declarations);
                package_fidl_json.declaration_order.append(&mut fidl_json.declaration_order);
            })
            .or_insert(fidl_json);
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use serde_json::json;

    #[test]
    fn sort_declarations_test() {
        let mut f = FidlJson {
            name: "fuchsia.test".to_string(),
            maybe_attributes: vec![json!({"name": "doc", "value": "Fuchsia Test API"})],
            library_dependencies: Vec::new(),
            bits_declarations: serde_json::from_str("[{\"name\": \"ABit\"},{\"name\": \"LastBit\"},{\"name\": \"AnotherBit\"}]").unwrap(),
            const_declarations: serde_json::from_str("[{\"name\": \"fuchsia.test/Const\"},{\"name\": \"fuchsia.test/AConst\"}]").unwrap(),
            enum_declarations: serde_json::from_str("[{\"name\": \"fuchsia.test/Enum\"},{\"name\": \"fuchsia.test/Third\"},{\"name\": \"fuchsia.test/Second\"}]").unwrap(),
            interface_declarations: serde_json::from_str("[{\"name\": \"Protocol1\",\"methods\": [{\"name\": \"Method 2\"},{\"name\": \"Method 1\"}]},{\"name\": \"AnotherProtocol\",\"methods\": [{\"name\": \"AMethod\"},{\"name\": \"BMethod\"}]}]").unwrap(),
            table_declarations: serde_json::from_str("[{\"name\": \"4\"},{\"name\": \"2A\"},{\"name\": \"11\"},{\"name\": \"zzz\"}]").unwrap(),
            type_alias_declarations: serde_json::from_str("[{\"name\": \"fuchsia.test/type\"},{\"name\": \"fuchsia.test/alias\"}]").unwrap(),
            struct_declarations: serde_json::from_str("[{\"name\": \"fuchsia.test/SomeLongAnonymousPrefix1\"},{\"name\": \"fuchsia.test/Struct\"},{\"name\": \"fuchsia.test/SomeLongAnonymousPrefix0\"}]").unwrap(),
            union_declarations: serde_json::from_str("[{\"name\": \"union1\"},{\"name\": \"Union1\"},{\"name\": \"UnIoN1\"}]").unwrap(),
            declaration_order: Vec::new(),
            declarations: Map::new(),
        };

        f.sort_declarations();

        assert_eq!(&f.bits_declarations[0]["name"], "ABit");
        assert_eq!(&f.bits_declarations[1]["name"], "AnotherBit");
        assert_eq!(&f.bits_declarations[2]["name"], "LastBit");

        assert_eq!(&f.const_declarations[0]["name"], "fuchsia.test/AConst");
        assert_eq!(&f.const_declarations[1]["name"], "fuchsia.test/Const");

        assert_eq!(&f.enum_declarations[0]["name"], "fuchsia.test/Enum");
        assert_eq!(&f.enum_declarations[1]["name"], "fuchsia.test/Second");
        assert_eq!(&f.enum_declarations[2]["name"], "fuchsia.test/Third");

        assert_eq!(&f.table_declarations[0]["name"], "11");
        assert_eq!(&f.table_declarations[1]["name"], "2A");
        assert_eq!(&f.table_declarations[2]["name"], "4");
        assert_eq!(&f.table_declarations[3]["name"], "zzz");

        assert_eq!(&f.type_alias_declarations[0]["name"], "fuchsia.test/alias");
        assert_eq!(&f.type_alias_declarations[1]["name"], "fuchsia.test/type");

        assert_eq!(&f.struct_declarations[0]["name"], "fuchsia.test/SomeLongAnonymousPrefix0");
        assert_eq!(&f.struct_declarations[1]["name"], "fuchsia.test/SomeLongAnonymousPrefix1");
        assert_eq!(&f.struct_declarations[2]["name"], "fuchsia.test/Struct");

        assert_eq!(&f.union_declarations[0]["name"], "UnIoN1");
        assert_eq!(&f.union_declarations[1]["name"], "Union1");
        assert_eq!(&f.union_declarations[2]["name"], "union1");
    }
}
