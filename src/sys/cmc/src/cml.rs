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
    pub static ref FROM_RE: Regex = Regex::new(r"^(realm|self|#[A-Za-z0-9\-_]+)$").unwrap();
}
pub const LAZY: &str = "lazy";
pub const EAGER: &str = "eager";
pub const PERSISTENT: &str = "persistent";
pub const TRANSIENT: &str = "transient";
pub const AMBIENT_PATHS: [&str; 1] = ["/svc/fuchsia.sys2.Realm"];

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
    pub directory: Option<String>,
    pub storage: Option<String>,
    pub r#as: Option<String>,
}

#[derive(Deserialize, Debug)]
pub struct Expose {
    pub service: Option<String>,
    pub directory: Option<String>,
    pub from: String,
    pub r#as: Option<String>,
}

#[derive(Deserialize, Debug)]
pub struct Offer {
    pub service: Option<String>,
    pub directory: Option<String>,
    pub storage: Option<String>,
    pub from: String,
    pub to: Vec<To>,
}

#[derive(Deserialize, Debug)]
pub struct To {
    pub dest: String,
    pub r#as: Option<String>,
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
    fn directory(&self) -> &Option<String>;
    fn storage(&self) -> &Option<String>;
}

pub trait AsClause {
    fn r#as(&self) -> &Option<String>;
}
pub trait DestClause {
    fn dest(&self) -> Option<&str>;
}

impl CapabilityClause for Use {
    fn service(&self) -> &Option<String> {
        &self.service
    }
    fn directory(&self) -> &Option<String> {
        &self.directory
    }
    fn storage(&self) -> &Option<String> {
        &self.storage
    }
}

impl AsClause for Use {
    fn r#as(&self) -> &Option<String> {
        &self.r#as
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
    fn directory(&self) -> &Option<String> {
        &self.directory
    }
    fn storage(&self) -> &Option<String> {
        &None
    }
}

impl AsClause for Expose {
    fn r#as(&self) -> &Option<String> {
        &self.r#as
    }
}

impl DestClause for Expose {
    fn dest(&self) -> Option<&str> {
        None
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
    fn directory(&self) -> &Option<String> {
        &self.directory
    }
    fn storage(&self) -> &Option<String> {
        &self.storage
    }
}

impl AsClause for To {
    fn r#as(&self) -> &Option<String> {
        &self.r#as
    }
}

impl DestClause for To {
    fn dest(&self) -> Option<&str> {
        Some(&self.dest)
    }
}

impl FromClause for Storage {
    fn from(&self) -> &str {
        &self.from
    }
}
