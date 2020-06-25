// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::one_or_many::OneOrMany,
    cm_json::{cm, Error},
    cmc_macro::{CheckedVec, OneOrMany, Ref},
    serde::{de, Deserialize},
    serde_json::{Map, Value},
    std::{collections::HashMap, fmt, str::FromStr},
};

pub use cm_types::{
    DependencyType, Durability, Name, ParseError, Path, RelativePath, StartupMode, StorageType, Url,
};

/// A string that could be a name or filesystem path.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum NameOrPath {
    Name(Name),
    Path(Path),
}

impl NameOrPath {
    /// Get the name if this is a name, or return an error if not.
    pub fn extract_name(self) -> Result<Name, Error> {
        match self {
            Self::Name(n) => Ok(n),
            Self::Path(_) => Err(Error::internal("not a name")),
        }
    }

    /// Get the name if this is a name, or return an error if not.
    pub fn extract_name_borrowed(&self) -> Result<&Name, Error> {
        match self {
            Self::Name(ref n) => Ok(n),
            Self::Path(_) => Err(Error::internal("not a name")),
        }
    }

    /// Get the path if this is a path, or return an error if not.
    pub fn extract_path(self) -> Result<Path, Error> {
        match self {
            Self::Path(p) => Ok(p),
            Self::Name(_) => Err(Error::internal("not a path")),
        }
    }

    /// Get the path if this is a path, or return an error if not.
    pub fn extract_path_borrowed(&self) -> Result<&Path, Error> {
        match self {
            Self::Path(ref p) => Ok(p),
            Self::Name(_) => Err(Error::internal("not a path")),
        }
    }
}

impl FromStr for NameOrPath {
    type Err = ParseError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(if s.starts_with("/") {
            NameOrPath::Path(s.parse()?)
        } else {
            NameOrPath::Name(s.parse()?)
        })
    }
}

impl<'de> de::Deserialize<'de> for NameOrPath {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: de::Deserializer<'de>,
    {
        struct Visitor;

        impl<'de> de::Visitor<'de> for Visitor {
            type Value = NameOrPath;

            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(
                    "a non-empty path no more than 1024 characters \
                     in length, with a leading `/`, and containing no \
                     empty path segments, or \
                     a non-empty string no more than 100 characters in \
                     length, containing only alpha-numeric characters",
                )
            }

            fn visit_str<E>(self, s: &str) -> Result<Self::Value, E>
            where
                E: de::Error,
            {
                s.parse().map_err(|err| match err {
                    ParseError::InvalidValue => E::invalid_value(
                        de::Unexpected::Str(s),
                        &"a path with leading `/` and non-empty segments, or \
                              a name containing only alpha-numeric characters or [_-.]",
                    ),
                    ParseError::InvalidLength => E::invalid_length(
                        s.len(),
                        &"a non-empty path no more than 1024 characters in length, or \
                              a non-empty name no more than 100 characters in length",
                    ),
                })
            }
        }
        deserializer.deserialize_string(Visitor)
    }
}

/// A list of offer targets.
#[derive(CheckedVec, Debug)]
#[expected = "a nonempty array of offer targets, with unique elements"]
#[min_length = 1]
#[unique_items = true]
pub struct OfferTo(pub Vec<OfferToRef>);

/// A list of rights.
#[derive(CheckedVec, Debug)]
#[expected = "a nonempty array of rights, with unique elements"]
#[min_length = 1]
#[unique_items = true]
pub struct Rights(pub Vec<Right>);

/// Generates deserializer for `OneOrMany<Name>`.
#[derive(OneOrMany, Debug, Clone)]
#[inner_type(Name)]
#[expected = "a name or nonempty array of names, with unique elements"]
#[min_length = 1]
#[unique_items = true]
pub struct OneOrManyNames;

/// Generates deserializer for `OneOrMany<Path>`.
#[derive(OneOrMany, Debug, Clone)]
#[inner_type(Path)]
#[expected = "a path or nonempty array of paths, with unique elements"]
#[min_length = 1]
#[unique_items = true]
pub struct OneOrManyPaths;

/// Generates deserializer for `OneOrMany<ExposeFromRef>`.
#[derive(OneOrMany, Debug, Clone)]
#[inner_type(ExposeFromRef)]
#[expected = "one or an array of \"framework\", \"self\", or \"#<child-name>\""]
#[min_length = 1]
#[unique_items = true]
pub struct OneOrManyExposeFromRefs;

/// Generates deserializer for `OneOrMany<OfferFromRef>`.
#[derive(OneOrMany, Debug, Clone)]
#[inner_type(OfferFromRef)]
#[expected = "one or an array of \"realm\", \"framework\", \"self\", or \"#<child-name>\""]
#[min_length = 1]
#[unique_items = true]
pub struct OneOrManyOfferFromRefs;

/// The stop timeout configured in an environment.
#[derive(Debug, Clone, Copy)]
pub struct StopTimeoutMs(pub u32);

impl<'de> de::Deserialize<'de> for StopTimeoutMs {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: de::Deserializer<'de>,
    {
        struct Visitor;

        impl<'de> de::Visitor<'de> for Visitor {
            type Value = StopTimeoutMs;

            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str("an unsigned 32-bit integer")
            }

            fn visit_i64<E>(self, v: i64) -> Result<Self::Value, E>
            where
                E: de::Error,
            {
                if v < 0 || v > i64::from(u32::max_value()) {
                    return Err(E::invalid_value(
                        de::Unexpected::Signed(v),
                        &"an unsigned 32-bit integer",
                    ));
                }
                Ok(StopTimeoutMs(v as u32))
            }
        }

        deserializer.deserialize_i64(Visitor)
    }
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
#[expected = "\"realm\", \"framework\", or none"]
pub enum UseFromRef {
    /// A reference to the containing realm.
    Realm,
    /// A reference to the framework.
    Framework,
}

/// A reference in an `expose from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "\"framework\", \"self\", or \"#<child-name>\""]
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
#[expected = "\"realm\", \"framework\", or none"]
pub enum ExposeToRef {
    /// A reference to the realm.
    Realm,
    /// A reference to the framework.
    Framework,
}

/// A reference in an `offer from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "\"realm\", \"framework\", \"self\", or \"#<child-name>\""]
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
#[expected = "\"realm\", \"framework\", \"self\", \"#<child-name>\", or \"#<collection-name>\""]
pub enum OfferToRef {
    /// A reference to a child or collection.
    Named(Name),
}

/// A reference in an environment.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "\"#<environment-name>\""]
pub enum EnvironmentRef {
    /// A reference to an environment defined in this component.
    Named(Name),
}

/// A reference in a `storage from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Ref)]
#[expected = "\"realm\", \"self\", or \"#<child-name>\""]
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
#[expected = "\"realm\", \"self\", or \"#<child-name>\""]
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
#[expected = "\"realm\", \"self\", or \"#<child-name>\""]
pub enum RegistrationRef {
    /// A reference to a child.
    Named(Name),
    /// A reference to the containing realm.
    Realm,
    /// A reference to this component.
    Self_,
}

/// A right or bundle of rights to apply to a directory.
#[derive(Deserialize, Clone, Debug, Eq, PartialEq, Hash)]
#[serde(rename_all = "snake_case")]
pub enum Right {
    // Individual
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

    // Aliass
    #[serde(rename = "r*")]
    ReadAlias,
    #[serde(rename = "w*")]
    WriteAlias,
    #[serde(rename = "x*")]
    ExecuteAlias,
    #[serde(rename = "rw*")]
    ReadWriteAlias,
    #[serde(rename = "rx*")]
    ReadExecuteAlias,
}

impl fmt::Display for Right {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            Self::Connect => "connect",
            Self::Enumerate => "enumerate",
            Self::Execute => "execute",
            Self::GetAttributes => "get_attributes",
            Self::ModifyDirectory => "modify_directory",
            Self::ReadBytes => "read_bytes",
            Self::Traverse => "traverse",
            Self::UpdateAttributes => "update_attributes",
            Self::WriteBytes => "write_bytes",
            Self::Admin => "admin",
            Self::ReadAlias => "r*",
            Self::WriteAlias => "w*",
            Self::ExecuteAlias => "x*",
            Self::ReadWriteAlias => "rw*",
            Self::ReadExecuteAlias => "rx*",
        };
        write!(f, "{}", s)
    }
}

impl Right {
    /// Expands this right or bundle or rights into a list of `cm::Right`.
    pub fn expand(&self) -> Vec<cm::Right> {
        match self {
            Self::Connect => vec![cm::Right::Connect],
            Self::Enumerate => vec![cm::Right::Enumerate],
            Self::Execute => vec![cm::Right::Execute],
            Self::GetAttributes => vec![cm::Right::GetAttributes],
            Self::ModifyDirectory => vec![cm::Right::ModifyDirectory],
            Self::ReadBytes => vec![cm::Right::ReadBytes],
            Self::Traverse => vec![cm::Right::Traverse],
            Self::UpdateAttributes => vec![cm::Right::UpdateAttributes],
            Self::WriteBytes => vec![cm::Right::WriteBytes],
            Self::Admin => vec![cm::Right::Admin],
            Self::ReadAlias => vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::ReadBytes,
                cm::Right::GetAttributes,
            ],
            Self::WriteAlias => vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::WriteBytes,
                cm::Right::ModifyDirectory,
                cm::Right::UpdateAttributes,
            ],
            Self::ExecuteAlias => vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::Execute,
            ],
            Self::ReadWriteAlias => vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::ReadBytes,
                cm::Right::WriteBytes,
                cm::Right::ModifyDirectory,
                cm::Right::GetAttributes,
                cm::Right::UpdateAttributes,
            ],
            Self::ReadExecuteAlias => vec![
                cm::Right::Connect,
                cm::Right::Enumerate,
                cm::Right::Traverse,
                cm::Right::ReadBytes,
                cm::Right::GetAttributes,
                cm::Right::Execute,
            ],
        }
    }
}

/// Converts a set of cml right tokens (including aliases) into a well formed cm::Rights expansion.
/// A `cm::RightsValidationError` is returned on invalid rights.
pub fn parse_rights(right_tokens: &Rights) -> Result<cm::Rights, cm::RightsValidationError> {
    let mut rights = Vec::<cm::Right>::new();
    for right_token in right_tokens.0.iter() {
        rights.append(&mut right_token.expand());
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
    pub fn all_event_names(&self) -> Result<Vec<Name>, Error> {
        let mut all_events: Vec<Name> = vec![];
        if let Some(uses) = self.r#use.as_ref() {
            for use_ in uses.iter() {
                if let Some(event) = &use_.event {
                    let alias = use_.r#as();
                    let events: Vec<_> = event.to_vec();
                    if events.len() == 1 {
                        let event_name = alias_or_name(alias, &events[0])?.clone();
                        all_events.push(event_name);
                    } else {
                        let mut events = events.into_iter().cloned().collect();
                        all_events.append(&mut events);
                    }
                }
            }
        }
        Ok(all_events)
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
    pub stop_timeout_ms: Option<StopTimeoutMs>,
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
    pub service: Option<Path>,
    pub protocol: Option<OneOrMany<Path>>,
    pub directory: Option<Path>,
    pub storage: Option<StorageType>,
    pub runner: Option<Name>,
    pub from: Option<UseFromRef>,
    pub r#as: Option<NameOrPath>,
    pub rights: Option<Rights>,
    pub subdir: Option<RelativePath>,
    pub event: Option<OneOrMany<Name>>,
    pub event_stream: Option<OneOrMany<Name>>,
    pub filter: Option<Map<String, Value>>,
}

#[derive(Deserialize, Debug)]
pub struct Expose {
    pub service: Option<Path>,
    pub protocol: Option<OneOrMany<Path>>,
    pub directory: Option<Path>,
    pub runner: Option<Name>,
    pub resolver: Option<Name>,
    pub from: OneOrMany<ExposeFromRef>,
    pub r#as: Option<NameOrPath>,
    pub to: Option<ExposeToRef>,
    pub rights: Option<Rights>,
    pub subdir: Option<RelativePath>,
}

#[derive(Deserialize, Debug)]
pub struct Offer {
    pub service: Option<Path>,
    pub protocol: Option<OneOrMany<Path>>,
    pub directory: Option<Path>,
    pub storage: Option<StorageType>,
    pub runner: Option<Name>,
    pub resolver: Option<Name>,
    pub event: Option<OneOrMany<Name>>,
    pub from: OneOrMany<OfferFromRef>,
    pub to: OfferTo,
    pub r#as: Option<NameOrPath>,
    pub rights: Option<Rights>,
    pub subdir: Option<RelativePath>,
    pub dependency: Option<DependencyType>,
    pub filter: Option<Map<String, Value>>,
}

#[derive(Deserialize, Debug)]
pub struct Child {
    pub name: Name,
    pub url: Url,
    #[serde(default)]
    pub startup: StartupMode,
    pub environment: Option<EnvironmentRef>,
}

#[derive(Deserialize, Debug)]
pub struct Collection {
    pub name: Name,
    pub durability: Durability,
    pub environment: Option<EnvironmentRef>,
}

#[derive(Deserialize, Debug)]
pub struct Storage {
    pub name: Name,
    pub from: StorageFromRef,
    pub path: Path,
}

#[derive(Deserialize, Debug)]
pub struct Runner {
    pub name: Name,
    pub from: RunnerFromRef,
    pub path: Path,
}

#[derive(Deserialize, Debug)]
pub struct Resolver {
    pub name: Name,
    pub path: Path,
}

pub trait FromClause {
    fn from_(&self) -> OneOrMany<AnyRef<'_>>;
}

pub trait CapabilityClause {
    fn service(&self) -> &Option<Path>;
    fn protocol(&self) -> &Option<OneOrMany<Path>>;
    fn directory(&self) -> &Option<Path>;
    fn storage(&self) -> &Option<StorageType>;
    fn runner(&self) -> &Option<Name>;
    fn resolver(&self) -> &Option<Name>;
    fn event(&self) -> &Option<OneOrMany<Name>>;
    fn event_stream(&self) -> &Option<OneOrMany<Name>>;

    /// Returns the name of the capability for display purposes.
    /// If `service()` returns `Some`, the capability name must be "service", etc.
    fn capability_name(&self) -> &'static str;

    fn decl_type(&self) -> &'static str;
    fn supported(&self) -> &[&'static str];
}

pub trait AsClause {
    fn r#as(&self) -> Option<&NameOrPath>;
}

pub trait FilterClause {
    fn filter(&self) -> Option<&Map<String, Value>>;
}

impl CapabilityClause for Use {
    fn service(&self) -> &Option<Path> {
        &self.service
    }
    fn protocol(&self) -> &Option<OneOrMany<Path>> {
        &self.protocol
    }
    fn directory(&self) -> &Option<Path> {
        &self.directory
    }
    fn storage(&self) -> &Option<StorageType> {
        &self.storage
    }
    fn runner(&self) -> &Option<Name> {
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
    fn decl_type(&self) -> &'static str {
        "use"
    }
    fn supported(&self) -> &[&'static str] {
        &["service", "protocol", "directory", "storage", "runner", "event", "event_stream"]
    }
}

impl FilterClause for Use {
    fn filter(&self) -> Option<&Map<String, Value>> {
        self.filter.as_ref()
    }
}

impl AsClause for Use {
    fn r#as(&self) -> Option<&NameOrPath> {
        self.r#as.as_ref()
    }
}

impl FromClause for Expose {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        one_or_many_from_impl(&self.from)
    }
}

impl CapabilityClause for Expose {
    fn service(&self) -> &Option<Path> {
        &self.service
    }
    // TODO(340156): Only OneOrMany::One protocol is supported for now. Teach `expose` rules to accept
    // `Many` protocols.
    fn protocol(&self) -> &Option<OneOrMany<Path>> {
        &self.protocol
    }
    fn directory(&self) -> &Option<Path> {
        &self.directory
    }
    fn storage(&self) -> &Option<StorageType> {
        &None
    }
    fn runner(&self) -> &Option<Name> {
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
    fn decl_type(&self) -> &'static str {
        "expose"
    }
    fn supported(&self) -> &[&'static str] {
        &["service", "protocol", "directory", "runner", "resolver"]
    }
}

impl AsClause for Expose {
    fn r#as(&self) -> Option<&NameOrPath> {
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
    fn service(&self) -> &Option<Path> {
        &self.service
    }
    fn protocol(&self) -> &Option<OneOrMany<Path>> {
        &self.protocol
    }
    fn directory(&self) -> &Option<Path> {
        &self.directory
    }
    fn storage(&self) -> &Option<StorageType> {
        &self.storage
    }
    fn runner(&self) -> &Option<Name> {
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
        } else if self.event.is_some() {
            "event"
        } else {
            ""
        }
    }
    fn decl_type(&self) -> &'static str {
        "offer"
    }
    fn supported(&self) -> &[&'static str] {
        &["service", "protocol", "directory", "storage", "runner", "resolver", "event"]
    }
}

impl AsClause for Offer {
    fn r#as(&self) -> Option<&NameOrPath> {
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

pub(super) fn alias_or_name<'a>(
    alias: Option<&'a NameOrPath>,
    name: &'a Name,
) -> Result<&'a Name, Error> {
    Ok(alias.map(|a| a.extract_name_borrowed()).transpose()?.unwrap_or(name))
}

pub(super) fn alias_or_path<'a>(
    alias: Option<&'a NameOrPath>,
    path: &'a Path,
) -> Result<&'a Path, Error> {
    Ok(alias.map(|a| a.extract_path_borrowed()).transpose()?.unwrap_or(path))
}

#[cfg(test)]
mod tests {
    use super::*;
    use cm_json::{self, Error};
    use matches::assert_matches;
    use serde_json;

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

    macro_rules! test_parse_rights {
        (
            $(
                ($input:expr, $expected:expr),
            )+
        ) => {
            #[test]
            fn parse_rights() {
                $(
                    parse_rights_test($input, $expected);
                )+
            }
        }
    }

    fn parse_rights_test(input: &str, expected: Right) {
        let v = cm_json::from_json5_str(&format!("\"{}\"", input)).expect("invalid json");
        let r: Right = serde_json::from_value(v).expect("invalid right");
        assert_eq!(r, expected);
    }

    test_parse_rights! {
        ("connect", Right::Connect),
        ("enumerate", Right::Enumerate),
        ("execute", Right::Execute),
        ("get_attributes", Right::GetAttributes),
        ("modify_directory", Right::ModifyDirectory),
        ("read_bytes", Right::ReadBytes),
        ("traverse", Right::Traverse),
        ("update_attributes", Right::UpdateAttributes),
        ("write_bytes", Right::WriteBytes),
        ("admin", Right::Admin),
        ("r*", Right::ReadAlias),
        ("w*", Right::WriteAlias),
        ("x*", Right::ExecuteAlias),
        ("rw*", Right::ReadWriteAlias),
        ("rx*", Right::ReadExecuteAlias),
    }

    macro_rules! test_expand_rights {
        (
            $(
                ($input:expr, $expected:expr),
            )+
        ) => {
            #[test]
            fn expand_rights() {
                $(
                    expand_rights_test($input, $expected);
                )+
            }
        }
    }

    fn expand_rights_test(input: Right, expected: Vec<cm::Right>) {
        assert_eq!(input.expand(), expected);
    }

    test_expand_rights! {
        (Right::Connect, vec![cm::Right::Connect]),
        (Right::Enumerate, vec![cm::Right::Enumerate]),
        (Right::Execute, vec![cm::Right::Execute]),
        (Right::GetAttributes, vec![cm::Right::GetAttributes]),
        (Right::ModifyDirectory, vec![cm::Right::ModifyDirectory]),
        (Right::ReadBytes, vec![cm::Right::ReadBytes]),
        (Right::Traverse, vec![cm::Right::Traverse]),
        (Right::UpdateAttributes, vec![cm::Right::UpdateAttributes]),
        (Right::WriteBytes, vec![cm::Right::WriteBytes]),
        (Right::Admin, vec![cm::Right::Admin]),
        (Right::ReadAlias, vec![
            cm::Right::Connect,
            cm::Right::Enumerate,
            cm::Right::Traverse,
            cm::Right::ReadBytes,
            cm::Right::GetAttributes,
        ]),
        (Right::WriteAlias, vec![
            cm::Right::Connect,
            cm::Right::Enumerate,
            cm::Right::Traverse,
            cm::Right::WriteBytes,
            cm::Right::ModifyDirectory,
            cm::Right::UpdateAttributes,
        ]),
        (Right::ExecuteAlias, vec![
            cm::Right::Connect,
            cm::Right::Enumerate,
            cm::Right::Traverse,
            cm::Right::Execute,
        ]),
        (Right::ReadWriteAlias, vec![
            cm::Right::Connect,
            cm::Right::Enumerate,
            cm::Right::Traverse,
            cm::Right::ReadBytes,
            cm::Right::WriteBytes,
            cm::Right::ModifyDirectory,
            cm::Right::GetAttributes,
            cm::Right::UpdateAttributes,
        ]),
        (Right::ReadExecuteAlias, vec![
            cm::Right::Connect,
            cm::Right::Enumerate,
            cm::Right::Traverse,
            cm::Right::ReadBytes,
            cm::Right::GetAttributes,
            cm::Right::Execute,
        ]),
    }
}
