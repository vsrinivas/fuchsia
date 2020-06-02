// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::one_or_many::OneOrMany,
    cm_json::cm,
    cmc_macro::Ref,
    serde::Deserialize,
    serde_json::{Map, Value},
    std::{collections::HashMap, fmt},
    thiserror::Error,
};

pub use cm_types::Name;

pub const LAZY: &str = "lazy";
pub const EAGER: &str = "eager";
pub const PERSISTENT: &str = "persistent";
pub const TRANSIENT: &str = "transient";
pub const STRONG: &str = "strong";

/// An error that occurs during validation.
#[derive(Debug, Error)]
pub enum ValidationError {
    #[error("Named reference is not valid")]
    NamedRefInvalid(cm_types::NameValidationError),
    #[error("Reference is more than 100 characters")]
    RefTooLong,
    #[error("`use from` reference is not valid")]
    UseFromRefInvalid,
    #[error("`expose from` reference is not valid")]
    ExposeFromRefInvalid,
    #[error("`expose to` reference is not valid")]
    ExposeToRefInvalid,
    #[error("`offer from` reference is not valid")]
    OfferFromRefInvalid,
    #[error("`offer to` reference is not valid")]
    OfferToRefInvalid,
    #[error("environment reference is not valid")]
    EnvironmentRefInvalid,
    #[error("`storage from` reference is not valid")]
    StorageFromRefInvalid,
    #[error("`runner from` reference is not valid")]
    RunnerFromRefInvalid,
    #[error("Environment registration reference is not valid")]
    RegistrationRefInvalid,
}

/// A relative reference to another object. This is a generic type that can encode any supported
/// reference subtype. For named references, it holds a reference to the name instead of the name
/// itself.
///
/// Objects of this type are usually derived from conversions of context-specific reference
/// types that `#[derive(Ref)]`. This type makes it easy to write helper functions that operate on
/// generic references.
#[derive(Debug, PartialEq, Eq, Hash, Clone)]
pub enum AnyRef<'a> {
    /// A named reference. Parsed as `#name`, where `name` contains only
    /// alphanumeric characters, `-`, `_`, and `.`.
    Named(&'a Name),
    /// A reference to the realm. Parsed as `realm`.
    Realm,
    /// A reference to the framework (component manager). Parsed as `framework`.
    Framework,
    /// A reference to this component. Parsed as `self`.
    Self_,
}

/// Format an `AnyRef` as a string.
impl fmt::Display for AnyRef<'_> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Named(name) => write!(f, "#{}", name),
            Self::Realm => write!(f, "realm"),
            Self::Framework => write!(f, "framework"),
            Self::Self_ => write!(f, "self"),
        }
    }
}

/// A reference in a `use from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "a `use from` reference"]
#[parse_error(ValidationError::UseFromRefInvalid)]
pub enum UseFromRef {
    /// A reference to the containing realm.
    Realm,
    /// A reference to the framework.
    Framework,
}

/// A reference in an `expose from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "an `expose from` reference"]
#[parse_error(ValidationError::ExposeFromRefInvalid)]
pub enum ExposeFromRef {
    /// A reference to a child or collection.
    Named(Name),
    /// A reference to the framework.
    Framework,
    /// A reference to this component.
    Self_,
}

/// A reference in an `expose to`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "an `expose to` reference"]
#[parse_error(ValidationError::ExposeToRefInvalid)]
pub enum ExposeToRef {
    /// A reference to the realm.
    Realm,
    /// A reference to the framework.
    Framework,
}

/// A reference in an `offer from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "an `offer from` reference"]
#[parse_error(ValidationError::OfferFromRefInvalid)]
pub enum OfferFromRef {
    /// A reference to a child or collection.
    Named(Name),
    /// A reference to the realm.
    Realm,
    /// A reference to the framework.
    Framework,
    /// A reference to this component.
    Self_,
}

/// A reference in an `offer to`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "an `offer to` reference"]
#[parse_error(ValidationError::OfferToRefInvalid)]
pub enum OfferToRef {
    /// A reference to a child or collection.
    Named(Name),
}

/// A reference in an environment.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "an environment reference"]
#[parse_error(ValidationError::EnvironmentRefInvalid)]
pub enum EnvironmentRef {
    /// A reference to an environment defined in this component.
    Named(Name),
}

/// A reference in a `storage from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "a `storage from` reference"]
#[parse_error(ValidationError::StorageFromRefInvalid)]
pub enum StorageFromRef {
    /// A reference to a child.
    Named(Name),
    /// A reference to the realm.
    Realm,
    /// A reference to this component.
    Self_,
}

/// A reference in a `runner from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "a `runner from` reference"]
#[parse_error(ValidationError::RunnerFromRefInvalid)]
pub enum RunnerFromRef {
    /// A reference to a child.
    Named(Name),
    /// A reference to the realm.
    Realm,
    /// A reference to this component.
    Self_,
}

/// A reference in an environment registration.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "a registration reference"]
#[parse_error(ValidationError::RegistrationRefInvalid)]
pub enum RegistrationRef {
    /// A reference to a child.
    Named(Name),
    /// A reference to the containing realm.
    Realm,
    /// A reference to this component.
    Self_,
}

/// Converts a valid cml right (including aliases) into a Vec<cm::Right> expansion. This function
/// will expand all valid right aliases and pass through non-aliased rights through to
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
/// This function will expand all valid right aliases and pass through non-aliased rights through
/// to Rights::map_token. A cm::RightsValidationError will be returned on invalid rights.
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
    pub resolvers: Option<Vec<Resolver>>,
    pub environments: Option<Vec<Environment>>,
}

impl Document {
    pub fn all_event_names(&self) -> Vec<Name> {
        let mut all_events: Vec<Name> = vec![];
        if let Some(uses) = self.r#use.as_ref() {
            for use_ in uses.iter() {
                if let Some(event) = &use_.event {
                    let alias = use_.r#as();
                    let mut events =
                        event.to_vec().iter().map(|e| e.clone()).collect::<Vec<Name>>();

                    if events.len() == 1 {
                        let event_name = alias.unwrap_or(&events[0].to_string()).to_string();
                        if let Ok(event_name) = Name::new(event_name) {
                            all_events.push(event_name);
                        }
                    } else {
                        all_events.append(&mut events);
                    }
                }
            }
        }
        all_events
    }

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

    pub fn all_storage_and_sources<'a>(&'a self) -> HashMap<&'a Name, &'a StorageFromRef> {
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

    pub fn all_resolver_names(&self) -> Vec<&Name> {
        if let Some(resolvers) = self.resolvers.as_ref() {
            resolvers.iter().map(|s| &s.name).collect()
        } else {
            vec![]
        }
    }

    pub fn all_environment_names(&self) -> Vec<&Name> {
        if let Some(environments) = self.environments.as_ref() {
            environments.iter().map(|s| &s.name).collect()
        } else {
            vec![]
        }
    }
}

#[derive(Deserialize, Debug)]
#[serde(rename_all = "lowercase")]
pub enum EnvironmentExtends {
    Realm,
    None,
}

/// An Environment defines properties which affect the behavior of components within a realm, such
/// as its resolver.
#[derive(Deserialize, Debug)]
pub struct Environment {
    /// This name is used to reference the environment assigned to the component's children
    pub name: Name,
    // Whether the environment state should extend its realm, or start with empty property set.
    // When not set, its value is assumed to be EnvironmentExtends::None.
    pub extends: Option<EnvironmentExtends>,
    pub runners: Option<Vec<RunnerRegistration>>,
    pub resolvers: Option<Vec<ResolverRegistration>>,
    #[serde(rename(deserialize = "__stop_timeout_ms"))]
    pub stop_timeout_ms: Option<u32>,
}

#[derive(Deserialize, Debug)]
pub struct RunnerRegistration {
    pub runner: Name,
    pub from: RegistrationRef,
    pub r#as: Option<Name>,
}

#[derive(Deserialize, Debug)]
pub struct ResolverRegistration {
    pub resolver: Name,
    pub from: RegistrationRef,
    pub scheme: cm_types::UrlScheme,
}

#[derive(Deserialize, Debug)]
pub struct Use {
    pub service: Option<String>,
    pub protocol: Option<OneOrMany<String>>,
    pub directory: Option<String>,
    pub storage: Option<String>,
    pub runner: Option<String>,
    pub from: Option<UseFromRef>,
    pub r#as: Option<String>,
    pub rights: Option<Vec<String>>,
    pub subdir: Option<String>,
    pub event: Option<OneOrMany<Name>>,
    pub event_stream: Option<OneOrMany<Name>>,
    pub filter: Option<Map<String, Value>>,
}

#[derive(Deserialize, Debug)]
pub struct Expose {
    pub service: Option<String>,
    pub protocol: Option<OneOrMany<String>>,
    pub directory: Option<String>,
    pub runner: Option<String>,
    pub resolver: Option<Name>,
    pub from: OneOrMany<ExposeFromRef>,
    pub r#as: Option<String>,
    pub to: Option<ExposeToRef>,
    pub rights: Option<Vec<String>>,
    pub subdir: Option<String>,
}

#[derive(Deserialize, Debug)]
pub struct Offer {
    pub service: Option<String>,
    pub protocol: Option<OneOrMany<String>>,
    pub directory: Option<String>,
    pub storage: Option<String>,
    pub runner: Option<String>,
    pub resolver: Option<Name>,
    pub event: Option<OneOrMany<Name>>,
    pub from: OneOrMany<OfferFromRef>,
    pub to: Vec<OfferToRef>,
    pub r#as: Option<String>,
    pub rights: Option<Vec<String>>,
    pub subdir: Option<String>,
    pub dependency: Option<String>,
    pub filter: Option<Map<String, Value>>,
}

#[derive(Deserialize, Debug)]
pub struct Child {
    pub name: Name,
    pub url: String,
    pub startup: Option<String>,
    pub environment: Option<EnvironmentRef>,
}

#[derive(Deserialize, Debug)]
pub struct Collection {
    pub name: Name,
    pub durability: String,
}

#[derive(Deserialize, Debug)]
pub struct Storage {
    pub name: Name,
    pub from: StorageFromRef,
    pub path: String,
}

#[derive(Deserialize, Debug)]
pub struct Runner {
    pub name: Name,
    pub from: RunnerFromRef,
    pub path: String,
}

#[derive(Deserialize, Debug)]
pub struct Resolver {
    pub name: Name,
    pub path: String,
}

pub trait FromClause {
    fn from_(&self) -> OneOrMany<AnyRef<'_>>;
}

pub trait CapabilityClause {
    fn service(&self) -> &Option<String>;
    fn protocol(&self) -> &Option<OneOrMany<String>>;
    fn directory(&self) -> &Option<String>;
    fn storage(&self) -> &Option<String>;
    fn runner(&self) -> &Option<String>;
    fn resolver(&self) -> &Option<Name>;
    fn event(&self) -> &Option<OneOrMany<Name>>;
    fn event_stream(&self) -> &Option<OneOrMany<Name>>;

    /// Returns the name of the capability for display purposes.
    /// If `service()` returns `Some`, the capability name must be "service", etc.
    fn capability_name(&self) -> &'static str;
}

pub trait AsClause {
    fn r#as(&self) -> Option<&String>;
}

pub trait FilterClause {
    fn filter(&self) -> Option<&Map<String, Value>>;
}

impl CapabilityClause for Use {
    fn service(&self) -> &Option<String> {
        &self.service
    }
    fn protocol(&self) -> &Option<OneOrMany<String>> {
        &self.protocol
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
    fn resolver(&self) -> &Option<Name> {
        &None
    }
    fn event(&self) -> &Option<OneOrMany<Name>> {
        &self.event
    }
    fn event_stream(&self) -> &Option<OneOrMany<Name>> {
        &self.event_stream
    }
    fn capability_name(&self) -> &'static str {
        if self.service.is_some() {
            "service"
        } else if self.protocol.is_some() {
            "protocol"
        } else if self.directory.is_some() {
            "directory"
        } else if self.storage.is_some() {
            "storage"
        } else if self.runner.is_some() {
            "runner"
        } else if self.event.is_some() {
            "event"
        } else if self.event_stream.is_some() {
            "event_stream"
        } else {
            ""
        }
    }
}

impl FilterClause for Use {
    fn filter(&self) -> Option<&Map<String, Value>> {
        self.filter.as_ref()
    }
}

impl AsClause for Use {
    fn r#as(&self) -> Option<&String> {
        self.r#as.as_ref()
    }
}

impl FromClause for Expose {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        one_or_many_from_impl(&self.from)
    }
}

impl CapabilityClause for Expose {
    fn service(&self) -> &Option<String> {
        &self.service
    }
    // TODO(340156): Only OneOrMany::One protocol is supported for now. Teach `expose` rules to accept
    // `Many` protocols.
    fn protocol(&self) -> &Option<OneOrMany<String>> {
        &self.protocol
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
    fn resolver(&self) -> &Option<Name> {
        &self.resolver
    }
    fn event(&self) -> &Option<OneOrMany<Name>> {
        &None
    }
    fn event_stream(&self) -> &Option<OneOrMany<Name>> {
        &None
    }
    fn capability_name(&self) -> &'static str {
        if self.service.is_some() {
            "service"
        } else if self.protocol.is_some() {
            "protocol"
        } else if self.directory.is_some() {
            "directory"
        } else if self.runner.is_some() {
            "runner"
        } else if self.resolver.is_some() {
            "resolver"
        } else {
            ""
        }
    }
}

impl AsClause for Expose {
    fn r#as(&self) -> Option<&String> {
        self.r#as.as_ref()
    }
}

impl FilterClause for Expose {
    fn filter(&self) -> Option<&Map<String, Value>> {
        None
    }
}

impl FromClause for Offer {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        one_or_many_from_impl(&self.from)
    }
}

impl CapabilityClause for Offer {
    fn service(&self) -> &Option<String> {
        &self.service
    }
    fn protocol(&self) -> &Option<OneOrMany<String>> {
        &self.protocol
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
    fn resolver(&self) -> &Option<Name> {
        &self.resolver
    }
    fn event(&self) -> &Option<OneOrMany<Name>> {
        &self.event
    }
    fn event_stream(&self) -> &Option<OneOrMany<Name>> {
        &None
    }
    fn capability_name(&self) -> &'static str {
        if self.service.is_some() {
            "service"
        } else if self.protocol.is_some() {
            "protocol"
        } else if self.directory.is_some() {
            "directory"
        } else if self.storage.is_some() {
            "storage"
        } else if self.runner.is_some() {
            "runner"
        } else if self.resolver.is_some() {
            "resolver"
        } else {
            ""
        }
    }
}

impl AsClause for Offer {
    fn r#as(&self) -> Option<&String> {
        self.r#as.as_ref()
    }
}

impl FilterClause for Offer {
    fn filter(&self) -> Option<&Map<String, Value>> {
        self.filter.as_ref()
    }
}

impl FromClause for Storage {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        OneOrMany::One(AnyRef::from(&self.from))
    }
}

impl FromClause for Runner {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        OneOrMany::One(AnyRef::from(&self.from))
    }
}

impl FromClause for RunnerRegistration {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        OneOrMany::One(AnyRef::from(&self.from))
    }
}

impl FromClause for ResolverRegistration {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        OneOrMany::One(AnyRef::from(&self.from))
    }
}

fn one_or_many_from_impl<'a, T>(from: &'a OneOrMany<T>) -> OneOrMany<AnyRef<'a>>
where
    AnyRef<'a>: From<&'a T>,
    T: 'a,
{
    let r = match from {
        OneOrMany::One(r) => OneOrMany::One(r.into()),
        OneOrMany::Many(v) => OneOrMany::Many(v.into_iter().map(|r| r.into()).collect()),
    };
    r.into()
}

#[cfg(test)]
mod tests {
    use super::*;
    use cm_json::{self, Error};
    use matches::assert_matches;

    // Exercise reference parsing tests on `OfferFromRef` because it contains every reference
    // subtype.

    #[test]
    fn test_parse_named_reference() {
        assert_matches!("#some-child".parse::<OfferFromRef>(), Ok(OfferFromRef::Named(name)) if name == "some-child");
        assert_matches!("#-".parse::<OfferFromRef>(), Ok(OfferFromRef::Named(name)) if name == "-");
        assert_matches!("#_".parse::<OfferFromRef>(), Ok(OfferFromRef::Named(name)) if name == "_");
        assert_matches!("#7".parse::<OfferFromRef>(), Ok(OfferFromRef::Named(name)) if name == "7");

        assert_matches!("#".parse::<OfferFromRef>(), Err(_));
        assert_matches!("some-child".parse::<OfferFromRef>(), Err(_));
    }

    #[test]
    fn test_parse_reference_test() {
        assert_matches!("realm".parse::<OfferFromRef>(), Ok(OfferFromRef::Realm));
        assert_matches!("realm".parse::<OfferFromRef>(), Ok(OfferFromRef::Realm));
        assert_matches!("framework".parse::<OfferFromRef>(), Ok(OfferFromRef::Framework));
        assert_matches!("self".parse::<OfferFromRef>(), Ok(OfferFromRef::Self_));
        assert_matches!("#child".parse::<OfferFromRef>(), Ok(OfferFromRef::Named(name)) if name == "child");

        assert_matches!("invalid".parse::<OfferFromRef>(), Err(_));
        assert_matches!("#invalid-child^".parse::<OfferFromRef>(), Err(_));
    }

    fn parse_as_ref(input: &str) -> Result<OfferFromRef, Error> {
        serde_json::from_value::<OfferFromRef>(cm_json::from_json_str(input)?)
            .map_err(|e| Error::parse(format!("{}", e)))
    }

    #[test]
    fn test_deserialize_ref() -> Result<(), Error> {
        assert_matches!(parse_as_ref("\"self\""), Ok(OfferFromRef::Self_));
        assert_matches!(parse_as_ref("\"realm\""), Ok(OfferFromRef::Realm));
        assert_matches!(parse_as_ref("\"#child\""), Ok(OfferFromRef::Named(name)) if name == "child");

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
