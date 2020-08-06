// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::Error,
    crate::one_or_many::OneOrMany,
    cmc_macro::{CheckedVec, OneOrMany, Reference},
    fidl_fuchsia_io2 as fio2,
    serde::{de, Deserialize},
    serde_json::{Map, Value},
    std::{collections::HashMap, fmt},
};

pub use cm_types::{
    DependencyType, Durability, Name, NameOrPath, ParseError, Path, RelativePath, StartupMode,
    StorageType, Url,
};

/// A list of offer targets.
#[derive(CheckedVec, Debug)]
#[checked_vec(
    expected = "a nonempty array of offer targets, with unique elements",
    min_length = 1,
    unique_items = true
)]
pub struct OfferTo(pub Vec<OfferToRef>);

/// A list of rights.
#[derive(CheckedVec, Debug)]
#[checked_vec(
    expected = "a nonempty array of rights, with unique elements",
    min_length = 1,
    unique_items = true
)]
pub struct Rights(pub Vec<Right>);

/// Generates deserializer for `OneOrMany<Name>`.
#[derive(OneOrMany, Debug, Clone)]
#[one_or_many(
    expected = "a name or nonempty array of names, with unique elements",
    inner_type = "Name",
    min_length = 1,
    unique_items = true
)]
pub struct OneOrManyNames;

/// Generates deserializer for `OneOrMany<Path>`.
#[derive(OneOrMany, Debug, Clone)]
#[one_or_many(
    expected = "a path or nonempty array of paths, with unique elements",
    inner_type = "Path",
    min_length = 1,
    unique_items = true
)]
pub struct OneOrManyPaths;

/// Generates deserializer for `OneOrMany<Path>`.
#[derive(OneOrMany, Debug, Clone)]
#[one_or_many(
    expected = "a name or path or nonempty array of names or paths, with unique elements",
    inner_type = "NameOrPath",
    min_length = 1,
    unique_items = true
)]
pub struct OneOrManyNameOrPaths;

/// Generates deserializer for `OneOrMany<ExposeFromRef>`.
#[derive(OneOrMany, Debug, Clone)]
#[one_or_many(
    expected = "one or an array of \"framework\", \"self\", or \"#<child-name>\"",
    inner_type = "ExposeFromRef",
    min_length = 1,
    unique_items = true
)]
pub struct OneOrManyExposeFromRefs;

/// Generates deserializer for `OneOrMany<OfferFromRef>`.
#[derive(OneOrMany, Debug, Clone)]
#[one_or_many(
    expected = "one or an array of \"parent\", \"framework\", \"self\", or \"#<child-name>\"",
    inner_type = "OfferFromRef",
    min_length = 1,
    unique_items = true
)]
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
/// types that `#[derive(Reference)]`. This type makes it easy to write helper functions that operate on
/// generic references.
#[derive(Debug, PartialEq, Eq, Hash, Clone)]
pub enum AnyRef<'a> {
    /// A named reference. Parsed as `#name`, where `name` contains only
    /// alphanumeric characters, `-`, `_`, and `.`.
    Named(&'a Name),
    /// A reference to the parent. Parsed as `parent`.
    Parent,
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
            Self::Parent => write!(f, "parent"),
            Self::Framework => write!(f, "framework"),
            Self::Self_ => write!(f, "self"),
        }
    }
}

/// A reference in a `use from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Reference)]
#[reference(expected = "\"parent\", \"framework\", or none")]
pub enum UseFromRef {
    /// A reference to the parent.
    Parent,
    /// A reference to the framework.
    Framework,
}

/// A reference in an `expose from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Reference)]
#[reference(expected = "\"framework\", \"self\", or \"#<child-name>\"")]
pub enum ExposeFromRef {
    /// A reference to a child or collection.
    Named(Name),
    /// A reference to the framework.
    Framework,
    /// A reference to this component.
    Self_,
}

/// A reference in an `expose to`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Reference)]
#[reference(expected = "\"parent\", \"framework\", or none")]
pub enum ExposeToRef {
    /// A reference to the parent.
    Parent,
    /// A reference to the framework.
    Framework,
}

/// A reference in an `offer from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Reference)]
#[reference(expected = "\"parent\", \"framework\", \"self\", or \"#<child-name>\"")]
pub enum OfferFromRef {
    /// A reference to a child or collection.
    Named(Name),
    /// A reference to the parent.
    Parent,
    /// A reference to the framework.
    Framework,
    /// A reference to this component.
    Self_,
}

/// A reference in an `offer to`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Reference)]
#[reference(
    expected = "\"parent\", \"framework\", \"self\", \"#<child-name>\", or \"#<collection-name>\""
)]
pub enum OfferToRef {
    /// A reference to a child or collection.
    Named(Name),
}

/// A reference in an environment.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Reference)]
#[reference(expected = "\"#<environment-name>\"")]
pub enum EnvironmentRef {
    /// A reference to an environment defined in this component.
    Named(Name),
}

/// A reference in a `storage from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Reference)]
#[reference(expected = "\"parent\", \"self\", or \"#<child-name>\"")]
pub enum CapabilityFromRef {
    /// A reference to a child.
    Named(Name),
    /// A reference to the parent.
    Parent,
    /// A reference to this component.
    Self_,
}

/// A reference in an environment registration.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Reference)]
#[reference(expected = "\"parent\", \"self\", or \"#<child-name>\"")]
pub enum RegistrationRef {
    /// A reference to a child.
    Named(Name),
    /// A reference to the parent.
    Parent,
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
    /// Expands this right or bundle or rights into a list of `fio2::Operations`.
    pub fn expand(&self) -> Vec<fio2::Operations> {
        match self {
            Self::Connect => vec![fio2::Operations::Connect],
            Self::Enumerate => vec![fio2::Operations::Enumerate],
            Self::Execute => vec![fio2::Operations::Execute],
            Self::GetAttributes => vec![fio2::Operations::GetAttributes],
            Self::ModifyDirectory => vec![fio2::Operations::ModifyDirectory],
            Self::ReadBytes => vec![fio2::Operations::ReadBytes],
            Self::Traverse => vec![fio2::Operations::Traverse],
            Self::UpdateAttributes => vec![fio2::Operations::UpdateAttributes],
            Self::WriteBytes => vec![fio2::Operations::WriteBytes],
            Self::Admin => vec![fio2::Operations::Admin],
            Self::ReadAlias => vec![
                fio2::Operations::Connect,
                fio2::Operations::Enumerate,
                fio2::Operations::Traverse,
                fio2::Operations::ReadBytes,
                fio2::Operations::GetAttributes,
            ],
            Self::WriteAlias => vec![
                fio2::Operations::Connect,
                fio2::Operations::Enumerate,
                fio2::Operations::Traverse,
                fio2::Operations::WriteBytes,
                fio2::Operations::ModifyDirectory,
                fio2::Operations::UpdateAttributes,
            ],
            Self::ExecuteAlias => vec![
                fio2::Operations::Connect,
                fio2::Operations::Enumerate,
                fio2::Operations::Traverse,
                fio2::Operations::Execute,
            ],
            Self::ReadWriteAlias => vec![
                fio2::Operations::Connect,
                fio2::Operations::Enumerate,
                fio2::Operations::Traverse,
                fio2::Operations::ReadBytes,
                fio2::Operations::WriteBytes,
                fio2::Operations::ModifyDirectory,
                fio2::Operations::GetAttributes,
                fio2::Operations::UpdateAttributes,
            ],
            Self::ReadExecuteAlias => vec![
                fio2::Operations::Connect,
                fio2::Operations::Enumerate,
                fio2::Operations::Traverse,
                fio2::Operations::ReadBytes,
                fio2::Operations::GetAttributes,
                fio2::Operations::Execute,
            ],
        }
    }
}

#[derive(Deserialize, Debug)]
#[serde(deny_unknown_fields)]
pub struct Document {
    pub program: Option<Map<String, Value>>,
    pub r#use: Option<Vec<Use>>,
    pub expose: Option<Vec<Expose>>,
    pub offer: Option<Vec<Offer>>,
    pub capabilities: Option<Vec<Capability>>,
    pub children: Option<Vec<Child>>,
    pub collections: Option<Vec<Collection>>,
    pub facets: Option<Map<String, Value>>,
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
        if let Some(capabilities) = self.capabilities.as_ref() {
            capabilities
                .iter()
                .filter_map(|c| match c.storage.as_ref() {
                    Some(s) => Some(s),
                    None => None,
                })
                .collect()
        } else {
            vec![]
        }
    }

    pub fn all_storage_and_sources<'a>(&'a self) -> HashMap<&'a Name, &'a CapabilityFromRef> {
        if let Some(capabilities) = self.capabilities.as_ref() {
            capabilities
                .iter()
                .filter_map(|c| match (c.storage.as_ref(), c.from.as_ref()) {
                    (Some(s), Some(f)) => Some((s, f)),
                    _ => None,
                })
                .collect()
        } else {
            HashMap::new()
        }
    }

    pub fn all_service_names(&self) -> Vec<&Name> {
        self.capabilities
            .as_ref()
            .map(|c| c.iter().filter_map(|c| c.service.as_ref()).collect())
            .unwrap_or_else(|| vec![])
    }

    pub fn all_protocol_names(&self) -> Vec<&Name> {
        self.capabilities
            .as_ref()
            .map(|c| {
                c.iter()
                    .filter_map(|c| c.protocol.as_ref())
                    .map(|p| p.to_vec().into_iter())
                    .flatten()
                    .collect()
            })
            .unwrap_or_else(|| vec![])
    }

    pub fn all_directory_names(&self) -> Vec<&Name> {
        self.capabilities
            .as_ref()
            .map(|c| c.iter().filter_map(|c| c.directory.as_ref()).collect())
            .unwrap_or_else(|| vec![])
    }

    pub fn all_runner_names(&self) -> Vec<&Name> {
        self.capabilities
            .as_ref()
            .map(|c| c.iter().filter_map(|c| c.runner.as_ref()).collect())
            .unwrap_or_else(|| vec![])
    }

    pub fn all_resolver_names(&self) -> Vec<&Name> {
        self.capabilities
            .as_ref()
            .map(|c| c.iter().filter_map(|c| c.resolver.as_ref()).collect())
            .unwrap_or_else(|| vec![])
    }

    pub fn all_environment_names(&self) -> Vec<&Name> {
        self.environments
            .as_ref()
            .map(|c| c.iter().map(|s| &s.name).collect())
            .unwrap_or_else(|| vec![])
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
#[serde(deny_unknown_fields)]
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
#[serde(deny_unknown_fields)]
pub struct RunnerRegistration {
    pub runner: Name,
    pub from: RegistrationRef,
    pub r#as: Option<Name>,
}

#[derive(Deserialize, Debug)]
#[serde(deny_unknown_fields)]
pub struct ResolverRegistration {
    pub resolver: Name,
    pub from: RegistrationRef,
    pub scheme: cm_types::UrlScheme,
}

#[derive(Deserialize, Debug)]
#[serde(deny_unknown_fields)]
pub struct Capability {
    pub service: Option<Name>,
    pub protocol: Option<OneOrMany<Name>>,
    pub directory: Option<Name>,
    pub storage: Option<Name>,
    pub runner: Option<Name>,
    pub resolver: Option<Name>,
    pub from: Option<CapabilityFromRef>,
    pub path: Option<Path>,
    pub rights: Option<Rights>,
    pub backing_dir: Option<Name>,
}

#[derive(Deserialize, Debug)]
#[serde(deny_unknown_fields)]
pub struct Use {
    pub service: Option<Name>,
    pub protocol: Option<OneOrMany<NameOrPath>>,
    pub directory: Option<NameOrPath>,
    pub storage: Option<StorageType>,
    pub runner: Option<Name>,
    pub from: Option<UseFromRef>,
    pub path: Option<Path>,
    pub r#as: Option<NameOrPath>,
    pub rights: Option<Rights>,
    pub subdir: Option<RelativePath>,
    pub event: Option<OneOrMany<Name>>,
    pub event_stream: Option<OneOrMany<Name>>,
    pub filter: Option<Map<String, Value>>,
}

#[derive(Deserialize, Debug)]
#[serde(deny_unknown_fields)]
pub struct Expose {
    pub service: Option<Name>,
    pub protocol: Option<OneOrMany<NameOrPath>>,
    pub directory: Option<NameOrPath>,
    pub runner: Option<Name>,
    pub resolver: Option<Name>,
    pub from: OneOrMany<ExposeFromRef>,
    pub r#as: Option<NameOrPath>,
    pub to: Option<ExposeToRef>,
    pub rights: Option<Rights>,
    pub subdir: Option<RelativePath>,
}

#[derive(Deserialize, Debug)]
#[serde(deny_unknown_fields)]
pub struct Offer {
    pub service: Option<Name>,
    pub protocol: Option<OneOrMany<NameOrPath>>,
    pub directory: Option<NameOrPath>,
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
#[serde(deny_unknown_fields)]
pub struct Child {
    pub name: Name,
    pub url: Url,
    #[serde(default)]
    pub startup: StartupMode,
    pub environment: Option<EnvironmentRef>,
}

#[derive(Deserialize, Debug)]
#[serde(deny_unknown_fields)]
pub struct Collection {
    pub name: Name,
    pub durability: Durability,
    pub environment: Option<EnvironmentRef>,
}

pub trait FromClause {
    fn from_(&self) -> OneOrMany<AnyRef<'_>>;
}

/// Type for generic functions that need to adjust their behavior based on the type of clause
/// provided.
// TODO: Refactor the methods that use this so they don't require this, probably by breaking
// them up.
#[derive(Debug, PartialEq, Eq)]
pub enum RoutingClauseType {
    Capability,
    Use,
    Offer,
    Expose,
}

pub trait CapabilityClause {
    fn service(&self) -> &Option<Name>;
    fn protocol(&self) -> Option<OneOrMany<NameOrPath>>;
    fn directory(&self) -> Option<NameOrPath>;
    fn storage(&self) -> &Option<Name>;
    fn storage_type(&self) -> &Option<StorageType>;
    fn runner(&self) -> &Option<Name>;
    fn resolver(&self) -> &Option<Name>;
    fn event(&self) -> &Option<OneOrMany<Name>>;
    fn event_stream(&self) -> &Option<OneOrMany<Name>>;

    /// Returns the name of the capability for display purposes.
    /// If `service()` returns `Some`, the capability name must be "service", etc.
    ///
    /// Panics if a capability keyword is not set.
    fn capability_name(&self) -> &'static str;

    fn decl_type(&self) -> &'static str;
    fn supported(&self) -> &[&'static str];
}

pub trait AsClause {
    fn r#as(&self) -> Option<&NameOrPath>;
}

pub trait PathClause {
    fn path(&self) -> Option<&Path>;
}

pub trait FilterClause {
    fn filter(&self) -> Option<&Map<String, Value>>;
}

pub trait RightsClause {
    fn rights(&self) -> Option<&Rights>;
}

impl CapabilityClause for Capability {
    fn service(&self) -> &Option<Name> {
        &self.service
    }
    fn protocol(&self) -> Option<OneOrMany<NameOrPath>> {
        self.protocol.as_ref().map(|o| match o {
            OneOrMany::One(n) => OneOrMany::One(NameOrPath::Name(n.clone())),
            OneOrMany::Many(v) => {
                OneOrMany::Many(v.iter().map(|n| NameOrPath::Name(n.clone())).collect())
            }
        })
    }
    fn directory(&self) -> Option<NameOrPath> {
        self.directory.as_ref().map(|n| NameOrPath::Name(n.clone()))
    }
    fn storage(&self) -> &Option<Name> {
        &self.storage
    }
    fn storage_type(&self) -> &Option<StorageType> {
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
        } else if self.storage.is_some() {
            "storage"
        } else if self.runner.is_some() {
            "runner"
        } else if self.resolver.is_some() {
            "resolver"
        } else {
            panic!("Missing capability name")
        }
    }
    fn decl_type(&self) -> &'static str {
        "capability"
    }
    fn supported(&self) -> &[&'static str] {
        &["service", "protocol", "directory", "storage", "runner", "resolver"]
    }
}

impl AsClause for Capability {
    fn r#as(&self) -> Option<&NameOrPath> {
        None
    }
}

impl PathClause for Capability {
    fn path(&self) -> Option<&Path> {
        self.path.as_ref()
    }
}

impl FilterClause for Capability {
    fn filter(&self) -> Option<&Map<String, Value>> {
        None
    }
}

impl RightsClause for Capability {
    fn rights(&self) -> Option<&Rights> {
        self.rights.as_ref()
    }
}

impl CapabilityClause for Use {
    fn service(&self) -> &Option<Name> {
        &self.service
    }
    fn protocol(&self) -> Option<OneOrMany<NameOrPath>> {
        self.protocol.clone()
    }
    fn directory(&self) -> Option<NameOrPath> {
        self.directory.clone()
    }
    fn storage(&self) -> &Option<Name> {
        &None
    }
    fn storage_type(&self) -> &Option<StorageType> {
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
            panic!("Missing capability name")
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

impl PathClause for Use {
    fn path(&self) -> Option<&Path> {
        self.path.as_ref()
    }
}

impl FromClause for Expose {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        one_or_many_from_impl(&self.from)
    }
}

impl RightsClause for Use {
    fn rights(&self) -> Option<&Rights> {
        self.rights.as_ref()
    }
}

impl CapabilityClause for Expose {
    fn service(&self) -> &Option<Name> {
        &self.service
    }
    // TODO(340156): Only OneOrMany::One protocol is supported for now. Teach `expose` rules to accept
    // `Many` protocols.
    fn protocol(&self) -> Option<OneOrMany<NameOrPath>> {
        self.protocol.clone()
    }
    fn directory(&self) -> Option<NameOrPath> {
        self.directory.clone()
    }
    fn storage(&self) -> &Option<Name> {
        &None
    }
    fn storage_type(&self) -> &Option<StorageType> {
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
            panic!("Missing capability name")
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

impl PathClause for Expose {
    fn path(&self) -> Option<&Path> {
        None
    }
}

impl FilterClause for Expose {
    fn filter(&self) -> Option<&Map<String, Value>> {
        None
    }
}

impl RightsClause for Expose {
    fn rights(&self) -> Option<&Rights> {
        self.rights.as_ref()
    }
}

impl FromClause for Offer {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        one_or_many_from_impl(&self.from)
    }
}

impl CapabilityClause for Offer {
    fn service(&self) -> &Option<Name> {
        &self.service
    }
    fn protocol(&self) -> Option<OneOrMany<NameOrPath>> {
        self.protocol.clone()
    }
    fn directory(&self) -> Option<NameOrPath> {
        self.directory.clone()
    }
    fn storage(&self) -> &Option<Name> {
        &None
    }
    fn storage_type(&self) -> &Option<StorageType> {
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
            panic!("Missing capability name")
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

impl PathClause for Offer {
    fn path(&self) -> Option<&Path> {
        None
    }
}

impl FilterClause for Offer {
    fn filter(&self) -> Option<&Map<String, Value>> {
        self.filter.as_ref()
    }
}

impl RightsClause for Offer {
    fn rights(&self) -> Option<&Rights> {
        self.rights.as_ref()
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

pub(super) fn alias_or_name(alias: Option<&NameOrPath>, name: &Name) -> Result<Name, Error> {
    Ok(alias.map(|a| a.extract_name_borrowed()).transpose()?.unwrap_or(name).clone())
}

pub(super) fn alias_or_path(alias: Option<&NameOrPath>, path: &Path) -> Result<Path, Error> {
    Ok(alias.map(|a| a.extract_path_borrowed()).transpose()?.unwrap_or(path).clone())
}

pub(super) fn alias_or_name_or_path(alias: Option<&NameOrPath>, id: &NameOrPath) -> NameOrPath {
    alias.map(|a| a).unwrap_or(id).clone()
}

#[cfg(test)]
mod tests {
    use super::*;
    use cm_json::{self, Error};
    use matches::assert_matches;
    use serde_json;
    use serde_json5;
    use std::path::Path;

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
        assert_matches!("parent".parse::<OfferFromRef>(), Ok(OfferFromRef::Parent));
        assert_matches!("framework".parse::<OfferFromRef>(), Ok(OfferFromRef::Framework));
        assert_matches!("self".parse::<OfferFromRef>(), Ok(OfferFromRef::Self_));
        assert_matches!("#child".parse::<OfferFromRef>(), Ok(OfferFromRef::Named(name)) if name == "child");

        assert_matches!("invalid".parse::<OfferFromRef>(), Err(_));
        assert_matches!("#invalid-child^".parse::<OfferFromRef>(), Err(_));
    }

    fn parse_as_ref(input: &str) -> Result<OfferFromRef, Error> {
        serde_json::from_value::<OfferFromRef>(cm_json::from_json_str(
            input,
            &Path::new("test.cml"),
        )?)
        .map_err(|e| Error::parse(format!("{}", e), None, None))
    }

    #[test]
    fn test_deserialize_ref() -> Result<(), Error> {
        assert_matches!(parse_as_ref("\"self\""), Ok(OfferFromRef::Self_));
        assert_matches!(parse_as_ref("\"parent\""), Ok(OfferFromRef::Parent));
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
        let r: Right = serde_json5::from_str(&format!("\"{}\"", input)).expect("invalid json");
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

    fn expand_rights_test(input: Right, expected: Vec<fio2::Operations>) {
        assert_eq!(input.expand(), expected);
    }

    test_expand_rights! {
        (Right::Connect, vec![fio2::Operations::Connect]),
        (Right::Enumerate, vec![fio2::Operations::Enumerate]),
        (Right::Execute, vec![fio2::Operations::Execute]),
        (Right::GetAttributes, vec![fio2::Operations::GetAttributes]),
        (Right::ModifyDirectory, vec![fio2::Operations::ModifyDirectory]),
        (Right::ReadBytes, vec![fio2::Operations::ReadBytes]),
        (Right::Traverse, vec![fio2::Operations::Traverse]),
        (Right::UpdateAttributes, vec![fio2::Operations::UpdateAttributes]),
        (Right::WriteBytes, vec![fio2::Operations::WriteBytes]),
        (Right::Admin, vec![fio2::Operations::Admin]),
        (Right::ReadAlias, vec![
            fio2::Operations::Connect,
            fio2::Operations::Enumerate,
            fio2::Operations::Traverse,
            fio2::Operations::ReadBytes,
            fio2::Operations::GetAttributes,
        ]),
        (Right::WriteAlias, vec![
            fio2::Operations::Connect,
            fio2::Operations::Enumerate,
            fio2::Operations::Traverse,
            fio2::Operations::WriteBytes,
            fio2::Operations::ModifyDirectory,
            fio2::Operations::UpdateAttributes,
        ]),
        (Right::ExecuteAlias, vec![
            fio2::Operations::Connect,
            fio2::Operations::Enumerate,
            fio2::Operations::Traverse,
            fio2::Operations::Execute,
        ]),
        (Right::ReadWriteAlias, vec![
            fio2::Operations::Connect,
            fio2::Operations::Enumerate,
            fio2::Operations::Traverse,
            fio2::Operations::ReadBytes,
            fio2::Operations::WriteBytes,
            fio2::Operations::ModifyDirectory,
            fio2::Operations::GetAttributes,
            fio2::Operations::UpdateAttributes,
        ]),
        (Right::ReadExecuteAlias, vec![
            fio2::Operations::Connect,
            fio2::Operations::Enumerate,
            fio2::Operations::Traverse,
            fio2::Operations::ReadBytes,
            fio2::Operations::GetAttributes,
            fio2::Operations::Execute,
        ]),
    }

    #[test]
    fn test_deny_unknown_fields() {
        assert_matches!(serde_json5::from_str::<Document>("{ unknown: \"\" }"), Err(_));
        assert_matches!(serde_json5::from_str::<Environment>("{ unknown: \"\" }"), Err(_));
        assert_matches!(serde_json5::from_str::<RunnerRegistration>("{ unknown: \"\" }"), Err(_));
        assert_matches!(serde_json5::from_str::<ResolverRegistration>("{ unknown: \"\" }"), Err(_));
        assert_matches!(serde_json5::from_str::<Use>("{ unknown: \"\" }"), Err(_));
        assert_matches!(serde_json5::from_str::<Expose>("{ unknown: \"\" }"), Err(_));
        assert_matches!(serde_json5::from_str::<Offer>("{ unknown: \"\" }"), Err(_));
        assert_matches!(serde_json5::from_str::<Capability>("{ unknown: \"\" }"), Err(_));
        assert_matches!(serde_json5::from_str::<Child>("{ unknown: \"\" }"), Err(_));
        assert_matches!(serde_json5::from_str::<Collection>("{ unknown: \"\" }"), Err(_));
    }
}
