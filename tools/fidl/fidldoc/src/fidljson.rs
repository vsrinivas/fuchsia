// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde_derive::Deserialize;
use serde_derive::Serialize;
use serde_json::Map;
use serde_json::Value;

use std::collections::HashMap;
use std::error::Error;
use std::fs::File;
use std::io;
use std::io::prelude::*;
use std::path::PathBuf;

#[derive(Serialize, Deserialize)]
pub struct TableOfContentsItem {
    pub name: String,
    pub link: String,
    pub description: String,
}

#[derive(Clone, Serialize, Deserialize)]
pub struct FidlJson {
    pub version: String,
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
    pub xunion_declarations: Vec<Value>,
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
                    reason = why.description()
                );
                return Err(why);
            }
            Ok(file) => file,
        };

        let mut s = String::new();
        fidl_file.read_to_string(&mut s)?;

        Ok(serde_json::from_str(&s)?)
    }
}

pub struct FidlJsonPackageData {
    pub declarations: Vec<String>,
    pub fidl_json_map: HashMap<String, FidlJson>,
}
