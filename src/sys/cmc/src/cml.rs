// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use lazy_static::lazy_static;
use regex::Regex;
use serde_derive::Deserialize;
use serde_json::{Map, Value};
use std::collections::HashMap;

lazy_static! {
    pub static ref REFERENCE_RE: Regex = Regex::new(r"^#([A-Za-z0-9\-_]+)$").unwrap();
    pub static ref FROM_RE: Regex =
        Regex::new(r"^(realm|framework|self|#[A-Za-z0-9\-_]+)$").unwrap();
    pub static ref FROM_SELF_TOKEN: &'static str = "self";
}

lazy_static! {
    pub static ref RIGHT_TOKENS: HashMap<&'static str, Vec<Right>> = {
        let mut tokens = HashMap::new();
        tokens.insert(
            "r*",
            vec![
                Right::Connect,
                Right::Enumerate,
                Right::Traverse,
                Right::ReadBytes,
                Right::GetAttributes,
            ],
        );
        tokens.insert(
            "w*",
            vec![
                Right::Connect,
                Right::Enumerate,
                Right::Traverse,
                Right::WriteBytes,
                Right::ModifyDirectory,
            ],
        );
        tokens
            .insert("x*", vec![Right::Connect, Right::Enumerate, Right::Traverse, Right::Execute]);
        tokens.insert(
            "rw*",
            vec![
                Right::Connect,
                Right::Enumerate,
                Right::Traverse,
                Right::ReadBytes,
                Right::WriteBytes,
                Right::GetAttributes,
                Right::UpdateAttributes,
            ],
        );
        tokens.insert(
            "rx*",
            vec![
                Right::Connect,
                Right::Enumerate,
                Right::Traverse,
                Right::ReadBytes,
                Right::GetAttributes,
                Right::Execute,
            ],
        );
        tokens.insert("connect", vec![Right::Connect]);
        tokens.insert("enumerate", vec![Right::Enumerate]);
        tokens.insert("execute", vec![Right::Execute]);
        tokens.insert("get_attributes", vec![Right::GetAttributes]);
        tokens.insert("modify_directory", vec![Right::ModifyDirectory]);
        tokens.insert("read_bytes", vec![Right::ReadBytes]);
        tokens.insert("traverse", vec![Right::Traverse]);
        tokens.insert("update_attributes", vec![Right::UpdateAttributes]);
        tokens.insert("write_bytes", vec![Right::WriteBytes]);
        tokens.insert("admin", vec![Right::Admin]);
        tokens
    };
}

pub const LAZY: &str = "lazy";
pub const EAGER: &str = "eager";
pub const PERSISTENT: &str = "persistent";
pub const TRANSIENT: &str = "transient";

#[derive(Debug, Eq, PartialEq, Hash)]
pub enum Right {
    Connect,
    Enumerate,
    Execute,
    GetAttributes,
    ModifyDirectory,
    ReadBytes,
    Traverse,
    UpdateAttributes,
    WriteBytes,
    Admin,
}

#[derive(Deserialize, Debug)]
pub struct Document {
    pub program: Option<Map<String, Value>>,
    pub r#use: Option<Vec<Use>>,
    pub expose: Option<Vec<Expose>>,
    pub offer: Option<Vec<Offer>>,
    pub children: Option<Vec<Child>>,
    pub collections: Option<Vec<Collection>>,
    pub storage: Option<Vec<Storage>>,
    pub facets: Option<Map<String, Value>>,
}

impl Document {
    pub fn all_children_names(&self) -> Vec<&str> {
        if let Some(children) = self.children.as_ref() {
            children.iter().map(|c| c.name.as_str()).collect()
        } else {
            vec![]
        }
    }

    pub fn all_collection_names(&self) -> Vec<&str> {
        if let Some(collections) = self.collections.as_ref() {
            collections.iter().map(|c| c.name.as_str()).collect()
        } else {
            vec![]
        }
    }

    pub fn all_storage_names(&self) -> Vec<&str> {
        if let Some(storage) = self.storage.as_ref() {
            storage.iter().map(|s| s.name.as_str()).collect()
        } else {
            vec![]
        }
    }

    pub fn all_storage_and_sources<'a>(&'a self) -> HashMap<&'a str, &'a str> {
        if let Some(storage) = self.storage.as_ref() {
            storage.iter().map(|s| (s.name.as_str(), s.from.as_str())).collect()
        } else {
            HashMap::new()
        }
    }
}

#[derive(Deserialize, Debug)]
pub struct Use {
    pub service: Option<String>,
    pub legacy_service: Option<String>,
    pub directory: Option<String>,
    pub storage: Option<String>,
    pub from: Option<String>,
    pub r#as: Option<String>,
    pub rights: Option<Vec<String>>,
}

#[derive(Deserialize, Debug)]
pub struct Expose {
    pub service: Option<String>,
    pub legacy_service: Option<String>,
    pub directory: Option<String>,
    pub from: String,
    pub r#as: Option<String>,
    pub to: Option<String>,
    pub rights: Option<Vec<String>>,
}

#[derive(Deserialize, Debug)]
pub struct Offer {
    pub service: Option<String>,
    pub legacy_service: Option<String>,
    pub directory: Option<String>,
    pub storage: Option<String>,
    pub from: String,
    pub to: Vec<String>,
    pub r#as: Option<String>,
    pub rights: Option<Vec<String>>,
}

#[derive(Deserialize, Debug)]
pub struct Child {
    pub name: String,
    pub url: String,
    pub startup: Option<String>,
}

#[derive(Deserialize, Debug)]
pub struct Collection {
    pub name: String,
    pub durability: String,
}

#[derive(Deserialize, Debug)]
pub struct Storage {
    pub name: String,
    pub from: String,
    pub path: String,
}

pub trait FromClause {
    fn from(&self) -> &str;
}

pub trait CapabilityClause {
    fn service(&self) -> &Option<String>;
    fn legacy_service(&self) -> &Option<String>;
    fn directory(&self) -> &Option<String>;
    fn storage(&self) -> &Option<String>;
}

pub trait AsClause {
    fn r#as(&self) -> Option<&String>;
}

impl CapabilityClause for Use {
    fn service(&self) -> &Option<String> {
        &self.service
    }
    fn legacy_service(&self) -> &Option<String> {
        &self.legacy_service
    }
    fn directory(&self) -> &Option<String> {
        &self.directory
    }
    fn storage(&self) -> &Option<String> {
        &self.storage
    }
}

impl AsClause for Use {
    fn r#as(&self) -> Option<&String> {
        self.r#as.as_ref()
    }
}

impl FromClause for Expose {
    fn from(&self) -> &str {
        &self.from
    }
}

impl CapabilityClause for Expose {
    fn service(&self) -> &Option<String> {
        &self.service
    }
    fn legacy_service(&self) -> &Option<String> {
        &self.legacy_service
    }
    fn directory(&self) -> &Option<String> {
        &self.directory
    }
    fn storage(&self) -> &Option<String> {
        &None
    }
}

impl AsClause for Expose {
    fn r#as(&self) -> Option<&String> {
        self.r#as.as_ref()
    }
}

impl FromClause for Offer {
    fn from(&self) -> &str {
        &self.from
    }
}

impl CapabilityClause for Offer {
    fn service(&self) -> &Option<String> {
        &self.service
    }
    fn legacy_service(&self) -> &Option<String> {
        &self.legacy_service
    }
    fn directory(&self) -> &Option<String> {
        &self.directory
    }
    fn storage(&self) -> &Option<String> {
        &self.storage
    }
}

impl AsClause for Offer {
    fn r#as(&self) -> Option<&String> {
        self.r#as.as_ref()
    }
}

impl FromClause for Storage {
    fn from(&self) -> &str {
        &self.from
    }
}
