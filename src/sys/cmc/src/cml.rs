// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cm_json::cm;
use lazy_static::lazy_static;
use regex::Regex;
use serde_derive::Deserialize;
use serde_json::{Map, Value};
use std::collections::HashMap;
use std::fmt;

pub const LAZY: &str = "lazy";
pub const EAGER: &str = "eager";
pub const PERSISTENT: &str = "persistent";
pub const TRANSIENT: &str = "transient";

/// Name of an object, such as a collection, component, or storage.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Deserialize)]
pub struct Name(String);

impl Name {
    pub fn new(name: &str) -> Name {
        Name(name.to_string())
    }

    pub fn as_str<'a>(&'a self) -> &'a str {
        self.0.as_str()
    }

    pub fn to_string(&self) -> String {
        self.0.to_string()
    }
}

/// Format a `Ref` as a string.
impl fmt::Display for Name {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0.as_str())
    }
}

/// A relative reference to another object.
#[derive(Debug, PartialEq, Eq, Hash, Clone)]
pub enum Ref {
    Named(Name),
    Realm,
    Framework,
    Self_,
}

/// Format a `Ref` as a string.
impl fmt::Display for Ref {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Ref::Named(name) => write!(f, "#{}", name.as_str()),
            Ref::Realm => write!(f, "realm"),
            Ref::Framework => write!(f, "framework"),
            Ref::Self_ => write!(f, "self"),
        }
    }
}

/// Deserialize a string into a valid Ref.
impl<'de> serde::de::Deserialize<'de> for Ref {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: serde::de::Deserializer<'de>,
    {
        struct RefVisitor;
        impl<'de> serde::de::Visitor<'de> for RefVisitor {
            type Value = Ref;

            fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
                formatter.write_str("a relative object reference")
            }

            fn visit_str<E>(self, value: &str) -> Result<Self::Value, E>
            where
                E: serde::de::Error,
            {
                parse_reference(value)
                    .ok_or_else(|| E::custom(format!("invalid reference: \"{}\"", value)))
            }
        }
        deserializer.deserialize_str(RefVisitor)
    }
}

lazy_static! {
    static ref NAME_RE: Regex = Regex::new(r"^#([A-Za-z0-9\-_.]+)$").unwrap();
}

/// Parse the given name of the form `#some-name`, returning the
/// name of the target if it is a valid target name, or `None`
/// otherwise.
pub fn parse_named_reference(reference: &str) -> Option<Name> {
    NAME_RE.captures(reference).map_or(None, |c| c.get(1)).map(|c| Name::new(c.as_str()))
}

/// Parse the given relative reference, consisting of tokens such as
/// `realm` or `#child`. Returns None if the name could not be parsed.
pub fn parse_reference<'a>(value: &'a str) -> Option<Ref> {
    if value.starts_with("#") {
        return parse_named_reference(value).map(|c| Ref::Named(c));
    }
    match value {
        "framework" => Some(Ref::Framework),
        "realm" => Some(Ref::Realm),
        "self" => Some(Ref::Self_),
        _ => None,
    }
}

/// Converts a valid cml right (including aliases) into a Vec<cm::Right> expansion. This function will
/// expand all valid right aliases and pass through non-aliased rights through to
/// Rights::map_token. None will be returned for invalid rights.
pub fn parse_right_token(token: &str) -> Option<Vec<cm::Right>> {
    match token {
        "r*" => Some(vec![
            cm::Right::Connect,
            cm::Right::Enumerate,
            cm::Right::Traverse,
            cm::Right::ReadBytes,
            cm::Right::GetAttributes,
        ]),
        "w*" => Some(vec![
            cm::Right::Connect,
            cm::Right::Enumerate,
            cm::Right::Traverse,
            cm::Right::WriteBytes,
            cm::Right::ModifyDirectory,
            cm::Right::UpdateAttributes,
        ]),
        "x*" => Some(vec![
            cm::Right::Connect,
            cm::Right::Enumerate,
            cm::Right::Traverse,
            cm::Right::Execute,
        ]),
        "rw*" => Some(vec![
            cm::Right::Connect,
            cm::Right::Enumerate,
            cm::Right::Traverse,
            cm::Right::ReadBytes,
            cm::Right::WriteBytes,
            cm::Right::ModifyDirectory,
            cm::Right::GetAttributes,
            cm::Right::UpdateAttributes,
        ]),
        "rx*" => Some(vec![
            cm::Right::Connect,
            cm::Right::Enumerate,
            cm::Right::Traverse,
            cm::Right::ReadBytes,
            cm::Right::GetAttributes,
            cm::Right::Execute,
        ]),
        _ => {
            if let Some(right) = cm::Rights::map_token(token) {
                return Some(vec![right]);
            }
            None
        }
    }
}

/// Converts a set of cml right tokens (including aliases) into a well formed cm::Rights expansion.
/// This function will expand all valid right aliases and pass through non-aliased rights through to
/// Rights::map_token. A cm::RightsValidationError will be returned on invalid rights.
pub fn parse_rights(right_tokens: &Vec<String>) -> Result<cm::Rights, cm::RightsValidationError> {
    let mut rights = Vec::<cm::Right>::new();
    for right_token in right_tokens.iter() {
        match parse_right_token(right_token) {
            Some(mut expanded_rights) => rights.append(&mut expanded_rights),
            None => return Err(cm::RightsValidationError::UnknownRight),
        }
    }
    cm::Rights::new(rights)
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
    pub runners: Option<Vec<Runner>>,
}

impl Document {
    pub fn all_children_names(&self) -> Vec<&Name> {
        if let Some(children) = self.children.as_ref() {
            children.iter().map(|c| &c.name).collect()
        } else {
            vec![]
        }
    }

    pub fn all_collection_names(&self) -> Vec<&Name> {
        if let Some(collections) = self.collections.as_ref() {
            collections.iter().map(|c| &c.name).collect()
        } else {
            vec![]
        }
    }

    pub fn all_storage_names(&self) -> Vec<&Name> {
        if let Some(storage) = self.storage.as_ref() {
            storage.iter().map(|s| &s.name).collect()
        } else {
            vec![]
        }
    }

    pub fn all_storage_and_sources<'a>(&'a self) -> HashMap<&'a Name, &'a Ref> {
        if let Some(storage) = self.storage.as_ref() {
            storage.iter().map(|s| (&s.name, &s.from)).collect()
        } else {
            HashMap::new()
        }
    }

    pub fn all_runner_names(&self) -> Vec<&Name> {
        if let Some(runners) = self.runners.as_ref() {
            runners.iter().map(|s| &s.name).collect()
        } else {
            vec![]
        }
    }
}

#[derive(Deserialize, Debug, Clone)]
#[serde(untagged)]
pub enum OneOrMany<T> {
    One(T),
    Many(Vec<T>),
}

impl<T: Clone> OneOrMany<T> {
    pub fn to_vec(self: OneOrMany<T>) -> Vec<T> {
        match self {
            OneOrMany::One(x) => return vec![x],
            OneOrMany::Many(xs) => return xs.to_vec(),
        }
    }
}

#[derive(Deserialize, Debug)]
pub struct Use {
    pub service: Option<String>,
    pub legacy_service: Option<OneOrMany<String>>,
    pub directory: Option<String>,
    pub storage: Option<String>,
    pub runner: Option<String>,
    pub from: Option<Ref>,
    pub r#as: Option<String>,
    pub rights: Option<Vec<String>>,
}

#[derive(Deserialize, Debug)]
pub struct Expose {
    pub service: Option<String>,
    pub legacy_service: Option<OneOrMany<String>>,
    pub directory: Option<String>,
    pub runner: Option<String>,
    pub from: Ref,
    pub r#as: Option<String>,
    pub to: Option<Ref>,
    pub rights: Option<Vec<String>>,
}

#[derive(Deserialize, Debug)]
pub struct Offer {
    pub service: Option<String>,
    pub legacy_service: Option<OneOrMany<String>>,
    pub directory: Option<String>,
    pub storage: Option<String>,
    pub runner: Option<String>,
    pub from: Ref,
    pub to: Vec<Ref>,
    pub r#as: Option<String>,
    pub rights: Option<Vec<String>>,
}

#[derive(Deserialize, Debug)]
pub struct Child {
    pub name: Name,
    pub url: String,
    pub startup: Option<String>,
}

#[derive(Deserialize, Debug)]
pub struct Collection {
    pub name: Name,
    pub durability: String,
}

#[derive(Deserialize, Debug)]
pub struct Storage {
    pub name: Name,
    pub from: Ref,
    pub path: String,
}

#[derive(Deserialize, Debug)]
pub struct Runner {
    pub name: Name,
    pub from: Ref,
    pub path: String,
}

pub trait FromClause {
    fn from(&self) -> &Ref;
}

pub trait CapabilityClause {
    fn service(&self) -> &Option<String>;
    fn legacy_service(&self) -> &Option<OneOrMany<String>>;
    fn directory(&self) -> &Option<String>;
    fn storage(&self) -> &Option<String>;
    fn runner(&self) -> &Option<String>;
}

pub trait AsClause {
    fn r#as(&self) -> Option<&String>;
}

impl CapabilityClause for Use {
    fn service(&self) -> &Option<String> {
        &self.service
    }
    // TODO(340156): Only OneOrMany::One legacy_service is supported for now. Teach `use` rules to
    // accept OneOrMany::Many legacy_services.
    fn legacy_service(&self) -> &Option<OneOrMany<String>> {
        &self.legacy_service
    }
    fn directory(&self) -> &Option<String> {
        &self.directory
    }
    fn storage(&self) -> &Option<String> {
        &self.storage
    }
    fn runner(&self) -> &Option<String> {
        &self.runner
    }
}

impl AsClause for Use {
    fn r#as(&self) -> Option<&String> {
        self.r#as.as_ref()
    }
}

impl FromClause for Expose {
    fn from(&self) -> &Ref {
        &self.from
    }
}

impl CapabilityClause for Expose {
    fn service(&self) -> &Option<String> {
        &self.service
    }
    // TODO(340156): Only OneOrMany::One legacy_service is supported for now. Teach `expose` rules to accept
    // `Many` legacy_services.
    fn legacy_service(&self) -> &Option<OneOrMany<String>> {
        &self.legacy_service
    }
    fn directory(&self) -> &Option<String> {
        &self.directory
    }
    fn storage(&self) -> &Option<String> {
        &None
    }
    fn runner(&self) -> &Option<String> {
        &self.runner
    }
}

impl AsClause for Expose {
    fn r#as(&self) -> Option<&String> {
        self.r#as.as_ref()
    }
}

impl FromClause for Offer {
    fn from(&self) -> &Ref {
        &self.from
    }
}

impl CapabilityClause for Offer {
    fn service(&self) -> &Option<String> {
        &self.service
    }
    fn legacy_service(&self) -> &Option<OneOrMany<String>> {
        &self.legacy_service
    }
    fn directory(&self) -> &Option<String> {
        &self.directory
    }
    fn storage(&self) -> &Option<String> {
        &self.storage
    }
    fn runner(&self) -> &Option<String> {
        &self.runner
    }
}

impl AsClause for Offer {
    fn r#as(&self) -> Option<&String> {
        self.r#as.as_ref()
    }
}

impl FromClause for Storage {
    fn from(&self) -> &Ref {
        &self.from
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use cm_json::{self, Error};
    use test_util::assert_matches;

    #[test]
    fn test_parse_named_reference() {
        assert_eq!(parse_named_reference("#some-child"), Some(Name::new("some-child")));
        assert_eq!(parse_named_reference("#-"), Some(Name::new("-")));
        assert_eq!(parse_named_reference("#_"), Some(Name::new("_")));
        assert_eq!(parse_named_reference("#7"), Some(Name::new("7")));

        assert_eq!(parse_named_reference("#"), None);
        assert_eq!(parse_named_reference("some-child"), None);
    }

    #[test]
    fn test_parse_reference_test() {
        assert_eq!(parse_reference("realm"), Some(Ref::Realm));
        assert_eq!(parse_reference("framework"), Some(Ref::Framework));
        assert_eq!(parse_reference("self"), Some(Ref::Self_));
        assert_eq!(parse_reference("#child"), Some(Ref::Named(Name::new("child"))));

        assert_eq!(parse_reference("invalid"), None);
        assert_eq!(parse_reference("#invalid-child^"), None);
    }

    fn parse_as_ref(input: &str) -> Result<Ref, Error> {
        serde_json::from_value::<Ref>(cm_json::from_json_str(input)?)
            .map_err(|e| Error::parse(format!("{}", e)))
    }

    #[test]
    fn test_deserialize_ref() -> Result<(), Error> {
        assert_eq!(parse_as_ref("\"self\"")?, Ref::Self_);
        assert_eq!(parse_as_ref("\"realm\"")?, Ref::Realm);
        assert_eq!(parse_as_ref("\"#child\"")?, Ref::Named(Name::new("child")));

        assert_matches!(parse_as_ref(r#""invalid""#), Err(_));

        Ok(())
    }

    #[test]
    fn test_parse_rights() -> Result<(), Error> {
        assert_eq!(
            parse_rights(&vec!["r*".to_owned()])?,
            cm::Rights(vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::ReadBytes,
                cm::Right::GetAttributes,
            ])
        );
        assert_eq!(
            parse_rights(&vec!["w*".to_owned()])?,
            cm::Rights(vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::WriteBytes,
                cm::Right::ModifyDirectory,
                cm::Right::UpdateAttributes,
            ])
        );
        assert_eq!(
            parse_rights(&vec!["x*".to_owned()])?,
            cm::Rights(vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::Execute,
            ])
        );
        assert_eq!(
            parse_rights(&vec!["rw*".to_owned()])?,
            cm::Rights(vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::ReadBytes,
                cm::Right::WriteBytes,
                cm::Right::ModifyDirectory,
                cm::Right::GetAttributes,
                cm::Right::UpdateAttributes,
            ])
        );
        assert_eq!(
            parse_rights(&vec!["rx*".to_owned()])?,
            cm::Rights(vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::ReadBytes,
                cm::Right::GetAttributes,
                cm::Right::Execute,
            ])
        );
        assert_eq!(
            parse_rights(&vec!["rw*".to_owned(), "execute".to_owned()])?,
            cm::Rights(vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::ReadBytes,
                cm::Right::WriteBytes,
                cm::Right::ModifyDirectory,
                cm::Right::GetAttributes,
                cm::Right::UpdateAttributes,
                cm::Right::Execute,
            ])
        );
        assert_eq!(
            parse_rights(&vec!["connect".to_owned()])?,
            cm::Rights(vec![cm::Right::Connect])
        );
        assert_eq!(
            parse_rights(&vec!["connect".to_owned(), "read_bytes".to_owned()])?,
            cm::Rights(vec![cm::Right::Connect, cm::Right::ReadBytes])
        );
        assert_matches!(parse_rights(&vec![]), Err(cm::RightsValidationError::EmptyRight));
        assert_matches!(
            parse_rights(&vec!["connec".to_owned()]),
            Err(cm::RightsValidationError::UnknownRight)
        );
        assert_matches!(
            parse_rights(&vec!["connect".to_owned(), "connect".to_owned()]),
            Err(cm::RightsValidationError::DuplicateRight)
        );
        assert_matches!(
            parse_rights(&vec!["rw*".to_owned(), "connect".to_owned()]),
            Err(cm::RightsValidationError::DuplicateRight)
        );

        Ok(())
    }
}
