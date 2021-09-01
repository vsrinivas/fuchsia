// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A library of common utilities used by `cmc` and related tools.

pub mod error;
pub mod one_or_many;
pub mod translate;

use {
    crate::error::Error,
    crate::one_or_many::OneOrMany,
    cml_macro::{CheckedVec, OneOrMany, Reference},
    fidl_fuchsia_io2 as fio2,
    lazy_static::lazy_static,
    serde::{de, Deserialize},
    serde_json::{Map, Value},
    std::{
        collections::{HashMap, HashSet},
        fmt,
        hash::Hash,
        ops::Deref,
        path,
    },
};

pub use cm_types::{
    AllowedOffers, DependencyType, Durability, Name, OnTerminate, ParseError, Path, RelativePath,
    StartupMode, StorageId, Url,
};

lazy_static! {
    static ref DEFAULT_EVENT_STREAM_NAME: Name = "EventStream".parse().unwrap();
}

/// A name/identity of a capability exposed/offered to another component.
///
/// Exposed or offered capabilities have an identifier whose format
/// depends on the capability type. For directories and services this is
/// a path, while for storage this is a storage name. Paths and storage
/// names, however, are in different conceptual namespaces, and can't
/// collide with each other.
///
/// This enum allows such names to be specified disambuating what
/// namespace they are in.
#[derive(Debug, PartialEq, Eq, Hash, Clone)]
pub enum CapabilityId {
    Service(Name),
    Protocol(Name),
    Directory(Name),
    // A service in a `use` declaration has a target path in the component's namespace.
    UsedService(Path),
    // A protocol in a `use` declaration has a target path in the component's namespace.
    UsedProtocol(Path),
    // A directory in a `use` declaration has a target path in the component's namespace.
    UsedDirectory(Path),
    // A storage in a `use` declaration has a target path in the component's namespace.
    UsedStorage(Path),
    Storage(Name),
    Runner(Name),
    Resolver(Name),
    Event(Name),
    EventStream(Name),
}

/// Generates a `Vec<Name>` -> `Vec<CapabilityId>` conversion function.
macro_rules! capability_ids_from_names {
    ($name:ident, $variant:expr) => {
        fn $name(names: Vec<Name>) -> Vec<Self> {
            names.into_iter().map(|n| $variant(n)).collect()
        }
    };
}

/// Generates a `Vec<Path>` -> `Vec<CapabilityId>` conversion function.
macro_rules! capability_ids_from_paths {
    ($name:ident, $variant:expr) => {
        fn $name(paths: Vec<Path>) -> Vec<Self> {
            paths.into_iter().map(|p| $variant(p)).collect()
        }
    };
}

impl CapabilityId {
    /// Human readable description of this capability type.
    pub fn type_str(&self) -> &'static str {
        match self {
            CapabilityId::Service(_) => "service",
            CapabilityId::Protocol(_) => "protocol",
            CapabilityId::Directory(_) => "directory",
            CapabilityId::UsedService(_) => "service",
            CapabilityId::UsedProtocol(_) => "protocol",
            CapabilityId::UsedDirectory(_) => "directory",
            CapabilityId::UsedStorage(_) => "storage",
            CapabilityId::Storage(_) => "storage",
            CapabilityId::Runner(_) => "runner",
            CapabilityId::Resolver(_) => "resolver",
            CapabilityId::Event(_) => "event",
            CapabilityId::EventStream(_) => "event_stream",
        }
    }

    /// Return the directory containing the capability.
    pub fn get_dir_path(&self) -> Option<&path::Path> {
        match self {
            CapabilityId::UsedService(p) => path::Path::new(p.as_str()).parent(),
            CapabilityId::UsedProtocol(p) => path::Path::new(p.as_str()).parent(),
            CapabilityId::UsedDirectory(p) => Some(path::Path::new(p.as_str())),
            CapabilityId::UsedStorage(p) => Some(path::Path::new(p.as_str())),
            _ => None,
        }
    }

    /// Given a Use clause, return the set of target identifiers.
    ///
    /// When only one capability identifier is specified, the target identifier name is derived
    /// using the "path" clause. If a "path" clause is not specified, the target identifier is the
    /// same name as the source.
    ///
    /// When multiple capability identifiers are specified, the target names are the same as the
    /// source names.
    pub fn from_use(use_: &Use) -> Result<Vec<CapabilityId>, Error> {
        // TODO: Validate that exactly one of these is set.
        let alias = use_.path.as_ref();
        if let Some(n) = use_.service() {
            return Ok(Self::used_services_from(Self::get_one_or_many_svc_paths(
                n,
                alias,
                use_.capability_type(),
            )?));
        } else if let Some(n) = use_.protocol() {
            return Ok(Self::used_protocols_from(Self::get_one_or_many_svc_paths(
                n,
                alias,
                use_.capability_type(),
            )?));
        } else if let Some(_) = use_.directory.as_ref() {
            if use_.path.is_none() {
                return Err(Error::validate("\"path\" should be present for `use directory`."));
            }
            return Ok(vec![CapabilityId::UsedDirectory(use_.path.as_ref().unwrap().clone())]);
        } else if let Some(_) = use_.storage.as_ref() {
            if use_.path.is_none() {
                return Err(Error::validate("\"path\" should be present for `use storage`."));
            }
            return Ok(vec![CapabilityId::UsedStorage(use_.path.as_ref().unwrap().clone())]);
        } else if let Some(OneOrMany::One(n)) = use_.event.as_ref() {
            return Ok(vec![CapabilityId::Event(alias_or_name(use_.r#as.as_ref(), n))]);
        } else if let Some(OneOrMany::Many(events)) = use_.event.as_ref() {
            return match (use_.r#as.as_ref(), use_.filter.as_ref(), events.len()) {
                (Some(alias), _, 1) => Ok(vec![CapabilityId::Event(alias.clone())]),
                (None, Some(_), 1) => Ok(vec![CapabilityId::Event(events[0].clone())]),
                (Some(_), None, _) => Err(Error::validate(
                    "\"as\" can only be specified when one `event` is supplied",
                )),
                (None, Some(_), _) => Err(Error::validate(
                    "\"filter\" can only be specified when one `event` is supplied",
                )),
                (Some(_), Some(_), _) => Err(Error::validate(
                    "\"as\",\"filter\" can only be specified when one `event` is supplied",
                )),
                (None, None, _) => Ok(events
                    .iter()
                    .map(|event: &Name| CapabilityId::Event(event.clone()))
                    .collect()),
            };
        } else if let Some(name) = use_.event_stream() {
            return Ok(vec![CapabilityId::EventStream(name)]);
        }

        // Unsupported capability type.
        let supported_keywords = use_
            .supported()
            .into_iter()
            .map(|k| format!("\"{}\"", k))
            .collect::<Vec<_>>()
            .join(", ");
        Err(Error::validate(format!(
            "`{}` declaration is missing a capability keyword, one of: {}",
            use_.decl_type(),
            supported_keywords,
        )))
    }

    pub fn from_capability(capability: &Capability) -> Result<Vec<CapabilityId>, Error> {
        // TODO: Validate that exactly one of these is set.
        if let Some(n) = capability.service() {
            if n.is_many() && capability.path.is_some() {
                return Err(Error::validate(
                    "\"path\" can only be specified when one `service` is supplied.",
                ));
            }
            return Ok(Self::services_from(Self::get_one_or_many_names(
                n,
                None,
                capability.capability_type(),
            )?));
        } else if let Some(n) = capability.protocol() {
            if n.is_many() && capability.path.is_some() {
                return Err(Error::validate(
                    "\"path\" can only be specified when one `protocol` is supplied.",
                ));
            }
            return Ok(Self::protocols_from(Self::get_one_or_many_names(
                n,
                None,
                capability.capability_type(),
            )?));
        } else if let Some(n) = capability.directory() {
            return Ok(Self::directories_from(Self::get_one_or_many_names(
                n,
                None,
                capability.capability_type(),
            )?));
        } else if let Some(n) = capability.storage() {
            if capability.storage_id.is_none() {
                return Err(Error::validate(
                    "Storage declaration is missing \"storage_id\", but is required.",
                ));
            }
            return Ok(Self::storages_from(Self::get_one_or_many_names(
                n,
                None,
                capability.capability_type(),
            )?));
        } else if let Some(n) = capability.runner() {
            return Ok(Self::runners_from(Self::get_one_or_many_names(
                n,
                None,
                capability.capability_type(),
            )?));
        } else if let Some(n) = capability.resolver() {
            return Ok(Self::resolvers_from(Self::get_one_or_many_names(
                n,
                None,
                capability.capability_type(),
            )?));
        } else if let Some(n) = capability.event() {
            return Ok(Self::events_from(Self::get_one_or_many_names(
                n,
                None,
                capability.capability_type(),
            )?));
        }

        // Unsupported capability type.
        let supported_keywords = capability
            .supported()
            .into_iter()
            .map(|k| format!("\"{}\"", k))
            .collect::<Vec<_>>()
            .join(", ");
        Err(Error::validate(format!(
            "`{}` declaration is missing a capability keyword, one of: {}",
            capability.decl_type(),
            supported_keywords,
        )))
    }

    /// Given an Offer or Exposeclause, return the set of target identifiers.
    ///
    /// When only one capability identifier is specified, the target identifier name is derived
    /// using the "as" clause. If an "as" clause is not specified, the target identifier is the
    /// same name as the source.
    ///
    /// When multiple capability identifiers are specified, the target names are the same as the
    /// source names.
    pub fn from_offer_expose<T>(clause: &T) -> Result<Vec<CapabilityId>, Error>
    where
        T: CapabilityClause + AsClause + FilterClause + fmt::Debug,
    {
        // TODO: Validate that exactly one of these is set.
        let alias = clause.r#as();
        if let Some(n) = clause.service() {
            return Ok(Self::services_from(Self::get_one_or_many_names(
                n,
                alias,
                clause.capability_type(),
            )?));
        } else if let Some(n) = clause.protocol() {
            return Ok(Self::protocols_from(Self::get_one_or_many_names(
                n,
                alias,
                clause.capability_type(),
            )?));
        } else if let Some(n) = clause.directory() {
            return Ok(Self::directories_from(Self::get_one_or_many_names(
                n,
                alias,
                clause.capability_type(),
            )?));
        } else if let Some(n) = clause.storage() {
            return Ok(Self::storages_from(Self::get_one_or_many_names(
                n,
                alias,
                clause.capability_type(),
            )?));
        } else if let Some(n) = clause.runner() {
            return Ok(Self::runners_from(Self::get_one_or_many_names(
                n,
                alias,
                clause.capability_type(),
            )?));
        } else if let Some(n) = clause.resolver() {
            return Ok(Self::resolvers_from(Self::get_one_or_many_names(
                n,
                alias,
                clause.capability_type(),
            )?));
        } else if let Some(events) = clause.event() {
            return match events {
                event @ OneOrMany::One(_) => Ok(Self::events_from(Self::get_one_or_many_names(
                    event,
                    alias,
                    clause.capability_type(),
                )?)),
                OneOrMany::Many(events) => match (alias, clause.filter(), events.len()) {
                    (Some(alias), _, 1) => Ok(vec![CapabilityId::Event(alias.clone())]),
                    (None, Some(_), 1) => Ok(vec![CapabilityId::Event(events[0].clone())]),
                    (Some(_), None, _) => Err(Error::validate(
                        "\"as\" can only be specified when one `event` is supplied",
                    )),
                    (None, Some(_), _) => Err(Error::validate(
                        "\"filter\" can only be specified when one `event` is supplied",
                    )),
                    (Some(_), Some(_), _) => Err(Error::validate(
                        "\"as\",\"filter\" can only be specified when one `event` is supplied",
                    )),
                    (None, None, _) => Ok(events
                        .iter()
                        .map(|event: &Name| CapabilityId::Event(event.clone()))
                        .collect()),
                },
            };
        }

        // Unsupported capability type.
        let supported_keywords = clause
            .supported()
            .into_iter()
            .map(|k| format!("\"{}\"", k))
            .collect::<Vec<_>>()
            .join(", ");
        Err(Error::validate(format!(
            "`{}` declaration is missing a capability keyword, one of: {}",
            clause.decl_type(),
            supported_keywords,
        )))
    }

    /// Returns the target names as a `Vec`  from a declaration with `names` and `alias` as a `Vec`.
    fn get_one_or_many_names(
        names: OneOrMany<Name>,
        alias: Option<&Name>,
        capability_type: &str,
    ) -> Result<Vec<Name>, Error> {
        let names: Vec<Name> = names.into_iter().collect();
        if names.len() == 1 {
            Ok(vec![alias_or_name(alias, &names[0])])
        } else {
            if alias.is_some() {
                return Err(Error::validate(format!(
                    "\"as\" can only be specified when one `{}` is supplied.",
                    capability_type,
                )));
            }
            Ok(names)
        }
    }

    /// Returns the target paths as a `Vec` from a `use` declaration with `names` and `alias`.
    fn get_one_or_many_svc_paths(
        names: OneOrMany<Name>,
        alias: Option<&Path>,
        capability_type: &str,
    ) -> Result<Vec<Path>, Error> {
        let names: Vec<Name> = names.into_iter().collect();
        match (names.len(), alias) {
            (_, None) => {
                Ok(names.into_iter().map(|n| format!("/svc/{}", n).parse().unwrap()).collect())
            }
            (1, Some(alias)) => Ok(vec![alias.clone()]),
            (_, Some(_)) => {
                return Err(Error::validate(format!(
                    "\"path\" can only be specified when one `{}` is supplied.",
                    capability_type,
                )));
            }
        }
    }

    capability_ids_from_names!(services_from, CapabilityId::Service);
    capability_ids_from_names!(protocols_from, CapabilityId::Protocol);
    capability_ids_from_names!(directories_from, CapabilityId::Directory);
    capability_ids_from_names!(storages_from, CapabilityId::Storage);
    capability_ids_from_names!(runners_from, CapabilityId::Runner);
    capability_ids_from_names!(resolvers_from, CapabilityId::Resolver);
    capability_ids_from_names!(events_from, CapabilityId::Event);
    capability_ids_from_paths!(used_services_from, CapabilityId::UsedService);
    capability_ids_from_paths!(used_protocols_from, CapabilityId::UsedProtocol);
}

impl fmt::Display for CapabilityId {
    /// Return the string ID of this clause.
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            CapabilityId::Service(n)
            | CapabilityId::Storage(n)
            | CapabilityId::Runner(n)
            | CapabilityId::Resolver(n)
            | CapabilityId::Event(n) => n.as_str(),
            CapabilityId::UsedService(p)
            | CapabilityId::UsedProtocol(p)
            | CapabilityId::UsedDirectory(p)
            | CapabilityId::UsedStorage(p) => p.as_str(),
            CapabilityId::EventStream(p) => p.as_str(),
            CapabilityId::Protocol(p) | CapabilityId::Directory(p) => p.as_str(),
        };
        write!(f, "{}", s)
    }
}

/// A list of rights.
#[derive(CheckedVec, Debug, PartialEq)]
#[checked_vec(
    expected = "a nonempty array of rights, with unique elements",
    min_length = 1,
    unique_items = true
)]
pub struct Rights(pub Vec<Right>);

/// A list of event modes.
#[derive(CheckedVec, Debug, PartialEq)]
#[checked_vec(
    expected = "a nonempty array of event modes, with unique elements",
    min_length = 1,
    unique_items = true
)]
pub struct EventModes(pub Vec<EventMode>);

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

/// Generates deserializer for `OneOrMany<ExposeFromRef>`.
#[derive(OneOrMany, Debug, Clone)]
#[one_or_many(
    expected = "one or an array of \"framework\", \"self\", or \"#<child-name>\"",
    inner_type = "ExposeFromRef",
    min_length = 1,
    unique_items = true
)]
pub struct OneOrManyExposeFromRefs;

/// Generates deserializer for `OneOrMany<OfferToRef>`.
#[derive(OneOrMany, Debug, Clone)]
#[one_or_many(
    expected = "one or an array of \"#<child-name>\" or \"#<collection-name>\", with unique elements",
    inner_type = "OfferToRef",
    min_length = 1,
    unique_items = true
)]
pub struct OneOrManyOfferToRefs;

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
#[derive(Debug, Clone, Copy, PartialEq)]
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
    /// A named reference. Parsed as `#name`.
    Named(&'a Name),
    /// A reference to the parent. Parsed as `parent`.
    Parent,
    /// A reference to the framework (component manager). Parsed as `framework`.
    Framework,

    /// A reference to the debug. Parsed as `debug`.
    Debug,
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
            Self::Debug => write!(f, "debug"),
            Self::Self_ => write!(f, "self"),
        }
    }
}

/// A reference in a `use from`.
#[derive(Debug, PartialEq, Eq, Hash, Clone, Reference)]
#[reference(
    expected = "\"parent\", \"framework\", \"debug\", \"self\", \"#<capability-name>\", \"#<child-name>\", or none"
)]
pub enum UseFromRef {
    /// A reference to the parent.
    Parent,
    /// A reference to the framework.
    Framework,
    /// A reference to debug.
    Debug,
    /// A reference to a child or a capability declared on self.
    ///
    /// A reference to a capability is only valid on use declarations for a protocol that
    /// references a storage capability declared in the same component, which will cause the
    /// framework to host a fuchsia.sys2.StorageAdmin protocol for the component.
    ///
    /// This cannot be used to directly access capabilities that a component itself declares.
    Named(Name),
    /// A reference to this component.
    Self_,
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

impl OfferFromRef {
    pub fn is_named(&self) -> bool {
        match self {
            OfferFromRef::Named(_) => true,
            _ => false,
        }
    }
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

#[derive(Deserialize, Clone, Debug, Eq, PartialEq, Hash)]
#[serde(rename_all = "snake_case")]
pub enum EventMode {
    /// Async events are allowed.
    Async,
    /// Sync events are allowed.
    Sync,
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct EventSubscription {
    pub event: OneOrMany<Name>,
    pub mode: Option<EventMode>,
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
    pub include: Option<Vec<String>>,
    pub program: Option<Program>,
    pub r#use: Option<Vec<Use>>,
    pub expose: Option<Vec<Expose>>,
    pub offer: Option<Vec<Offer>>,
    pub capabilities: Option<Vec<Capability>>,
    pub children: Option<Vec<Child>>,
    pub collections: Option<Vec<Collection>>,
    pub facets: Option<Map<String, Value>>,
    pub environments: Option<Vec<Environment>>,
}

macro_rules! merge_from_field {
    ($self:ident, $other:ident, $field_name:ident) => {
        if let Some(ref mut ours) = $self.$field_name {
            if let Some(theirs) = $other.$field_name.take() {
                // Add their elements, ignoring dupes with ours
                for t in theirs {
                    if !ours.contains(&t) {
                        ours.push(t);
                    }
                }
            }
        } else if let Some(theirs) = $other.$field_name.take() {
            $self.$field_name.replace(theirs);
        }
    };
}

impl Document {
    pub fn merge_from(
        &mut self,
        other: &mut Document,
        include_path: &path::Path,
    ) -> Result<(), Error> {
        merge_from_field!(self, other, include);
        merge_from_field!(self, other, r#use);
        merge_from_field!(self, other, expose);
        merge_from_field!(self, other, offer);
        merge_from_field!(self, other, capabilities);
        merge_from_field!(self, other, children);
        merge_from_field!(self, other, collections);
        merge_from_field!(self, other, environments);
        self.merge_program(other, include_path)?;
        // Facets aren't an actively used feature so we don't need to support them. Also,
        // the merge policy for facets would be non-trivial because they can contain nested maps.
        if let Some(_) = other.facets {
            return Err(Error::validate(format!(
                "facets found in manifest include, which are not supported: {}",
                include_path.display()
            )));
        }
        Ok(())
    }

    fn merge_program(
        &mut self,
        other: &mut Document,
        include_path: &path::Path,
    ) -> Result<(), Error> {
        if let None = other.program {
            return Ok(());
        }
        if let None = self.program {
            self.program = Some(Program::default());
        }
        let my_program = self.program.as_mut().unwrap();
        let other_program = other.program.as_mut().unwrap();
        if let Some(other_runner) = other_program.runner.take() {
            my_program.runner = match &my_program.runner {
                Some(runner) if *runner != other_runner => {
                    return Err(Error::validate(format!(
                        "manifest include had a conflicting `program.runner`: {}",
                        include_path.display()
                    )))
                }
                _ => Some(other_runner),
            }
        }

        for (key, value) in other_program.info.iter() {
            if let Some(_) = my_program.info.insert(key.clone(), value.clone()) {
                return Err(Error::validate(format!(
                    "manifest include had a conflicting `program.{}`: {}",
                    key,
                    include_path.display()
                )));
            }
        }

        Ok(())
    }

    pub fn includes(&self) -> Vec<String> {
        self.include.clone().unwrap_or_default()
    }

    pub fn all_event_names(&self) -> Result<Vec<Name>, Error> {
        let mut all_events: Vec<Name> = vec![];
        if let Some(uses) = self.r#use.as_ref() {
            for use_ in uses.iter() {
                if let Some(event) = &use_.event {
                    let alias = use_.r#as();
                    let events: Vec<_> = event.to_vec();
                    if events.len() == 1 {
                        let event_name = alias_or_name(alias, &events[0]).clone();
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
            capabilities.iter().filter_map(|c| c.storage.as_ref()).collect()
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
            .map(|c| {
                c.iter()
                    .filter_map(|c| c.service.as_ref())
                    .map(|p| p.to_vec().into_iter())
                    .flatten()
                    .collect()
            })
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

    pub fn all_capability_names(&self) -> HashSet<Name> {
        self.capabilities
            .as_ref()
            .map(|c| {
                c.iter().fold(HashSet::new(), |mut acc, capability| {
                    acc.extend(capability.names());
                    acc
                })
            })
            .unwrap_or_default()
    }
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum EnvironmentExtends {
    Realm,
    None,
}

/// An Environment defines properties which affect the behavior of components within a realm, such
/// as its resolver.
#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Environment {
    /// This name is used to reference the environment assigned to the component's children
    pub name: Name,
    // Whether the environment state should extend its realm, or start with empty property set.
    // When not set, its value is assumed to be EnvironmentExtends::None.
    pub extends: Option<EnvironmentExtends>,
    pub runners: Option<Vec<RunnerRegistration>>,
    pub resolvers: Option<Vec<ResolverRegistration>>,
    pub debug: Option<Vec<DebugRegistration>>,
    #[serde(rename(deserialize = "__stop_timeout_ms"))]
    pub stop_timeout_ms: Option<StopTimeoutMs>,
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct RunnerRegistration {
    pub runner: Name,
    pub from: RegistrationRef,
    pub r#as: Option<Name>,
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct ResolverRegistration {
    pub resolver: Name,
    pub from: RegistrationRef,
    pub scheme: cm_types::UrlScheme,
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Capability {
    pub service: Option<OneOrMany<Name>>,
    pub protocol: Option<OneOrMany<Name>>,
    pub directory: Option<Name>,
    pub storage: Option<Name>,
    pub runner: Option<Name>,
    pub resolver: Option<Name>,
    pub event: Option<Name>,
    pub from: Option<CapabilityFromRef>,
    pub path: Option<Path>,
    pub rights: Option<Rights>,
    pub backing_dir: Option<Name>,
    pub subdir: Option<RelativePath>,
    pub storage_id: Option<StorageId>,
    pub mode: Option<EventMode>,
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DebugRegistration {
    pub protocol: Option<OneOrMany<Name>>,
    pub from: OfferFromRef,
    pub r#as: Option<Name>,
    pub path: Option<Path>,
}

/// A list of event modes.
#[derive(CheckedVec, Debug, PartialEq)]
#[checked_vec(
    expected = "a nonempty array of event subscriptions",
    min_length = 1,
    unique_items = false
)]
pub struct EventSubscriptions(pub Vec<EventSubscription>);

impl Deref for EventSubscriptions {
    type Target = Vec<EventSubscription>;

    fn deref(&self) -> &Vec<EventSubscription> {
        &self.0
    }
}

#[derive(Debug, PartialEq, Default)]
pub struct Program {
    pub runner: Option<Name>,
    pub info: Map<String, Value>,
}

impl<'de> de::Deserialize<'de> for Program {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: de::Deserializer<'de>,
    {
        struct Visitor;

        const EXPECTED_PROGRAM: &'static str =
            "a JSON object that includes a `runner` string property";
        const EXPECTED_RUNNER: &'static str =
            "a non-empty `runner` string property no more than 100 characters in length \
            that consists of [A-Za-z0-9_.-] and starts with [A-Za-z0-9_]";

        impl<'de> de::Visitor<'de> for Visitor {
            type Value = Program;

            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str(EXPECTED_PROGRAM)
            }

            fn visit_map<A>(self, mut map: A) -> Result<Self::Value, A::Error>
            where
                A: de::MapAccess<'de>,
            {
                let mut info = Map::new();
                let mut runner = None;
                while let Some(e) = map.next_entry::<String, Value>()? {
                    let (k, v) = e;
                    if &k == "runner" {
                        if let Value::String(s) = v {
                            runner = Some(s);
                        } else {
                            return Err(de::Error::invalid_value(
                                de::Unexpected::Map,
                                &EXPECTED_RUNNER,
                            ));
                        }
                    } else {
                        info.insert(k, v);
                    }
                }
                let runner = runner
                    .map(|r| {
                        Name::new(r.clone()).map_err(|e| match e {
                            ParseError::InvalidValue => de::Error::invalid_value(
                                serde::de::Unexpected::Str(&r),
                                &EXPECTED_RUNNER,
                            ),
                            ParseError::InvalidLength => {
                                de::Error::invalid_length(r.len(), &EXPECTED_RUNNER)
                            }
                            _ => {
                                panic!("unexpected parse error: {:?}", e);
                            }
                        })
                    })
                    .transpose()?;
                Ok(Program { runner, info })
            }
        }

        deserializer.deserialize_map(Visitor)
    }
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Use {
    pub service: Option<OneOrMany<Name>>,
    pub protocol: Option<OneOrMany<Name>>,
    pub directory: Option<Name>,
    pub storage: Option<Name>,
    pub from: Option<UseFromRef>,
    pub path: Option<Path>,
    pub r#as: Option<Name>,
    pub rights: Option<Rights>,
    pub subdir: Option<RelativePath>,
    pub event: Option<OneOrMany<Name>>,
    pub event_stream: Option<Name>,
    pub filter: Option<Map<String, Value>>,
    pub modes: Option<EventModes>,
    pub subscriptions: Option<EventSubscriptions>,
    pub dependency: Option<DependencyType>,
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Expose {
    pub service: Option<OneOrMany<Name>>,
    pub protocol: Option<OneOrMany<Name>>,
    pub directory: Option<OneOrMany<Name>>,
    pub runner: Option<OneOrMany<Name>>,
    pub resolver: Option<OneOrMany<Name>>,
    pub from: OneOrMany<ExposeFromRef>,
    pub r#as: Option<Name>,
    pub to: Option<ExposeToRef>,
    pub rights: Option<Rights>,
    pub subdir: Option<RelativePath>,
    pub modes: Option<EventModes>,
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Offer {
    pub service: Option<OneOrMany<Name>>,
    pub protocol: Option<OneOrMany<Name>>,
    pub directory: Option<OneOrMany<Name>>,
    pub storage: Option<OneOrMany<Name>>,
    pub runner: Option<OneOrMany<Name>>,
    pub resolver: Option<OneOrMany<Name>>,
    pub event: Option<OneOrMany<Name>>,
    pub from: OneOrMany<OfferFromRef>,
    pub to: OneOrMany<OfferToRef>,
    pub r#as: Option<Name>,
    pub rights: Option<Rights>,
    pub subdir: Option<RelativePath>,
    pub dependency: Option<DependencyType>,
    pub filter: Option<Map<String, Value>>,
    pub modes: Option<EventModes>,
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Child {
    pub name: Name,
    pub url: Url,
    #[serde(default)]
    pub startup: StartupMode,
    pub on_terminate: Option<OnTerminate>,
    pub environment: Option<EnvironmentRef>,
}

#[derive(Deserialize, Debug, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct Collection {
    pub name: Name,
    pub durability: Durability,
    pub allowed_offers: Option<AllowedOffers>,
    pub environment: Option<EnvironmentRef>,
}

pub trait FromClause {
    fn from_(&self) -> OneOrMany<AnyRef<'_>>;
}

pub trait CapabilityClause {
    fn service(&self) -> Option<OneOrMany<Name>>;
    fn protocol(&self) -> Option<OneOrMany<Name>>;
    fn directory(&self) -> Option<OneOrMany<Name>>;
    fn storage(&self) -> Option<OneOrMany<Name>>;
    fn runner(&self) -> Option<OneOrMany<Name>>;
    fn resolver(&self) -> Option<OneOrMany<Name>>;
    fn event(&self) -> Option<OneOrMany<Name>>;
    fn event_stream(&self) -> Option<Name>;

    /// Returns the name of the capability for display purposes.
    /// If `service()` returns `Some`, the capability name must be "service", etc.
    ///
    /// Panics if a capability keyword is not set.
    fn capability_type(&self) -> &'static str;

    fn decl_type(&self) -> &'static str;
    fn supported(&self) -> &[&'static str];

    /// Returns the names of the capabilities in this clause.
    /// If `protocol()` returns `Some(OneOrMany::Many(vec!["a", "b"]))`, this returns!["a", "b"].
    fn names(&self) -> Vec<Name> {
        let res = vec![
            self.service(),
            self.protocol(),
            self.directory(),
            self.storage(),
            self.runner(),
            self.resolver(),
            self.event(),
            self.event_stream().map(|n| OneOrMany::One(n)),
        ];
        res.into_iter()
            .map(|o| o.map(|o| o.into_iter().collect::<Vec<Name>>()).unwrap_or(vec![]))
            .flatten()
            .collect()
    }
}

pub trait AsClause {
    fn r#as(&self) -> Option<&Name>;
}

pub trait PathClause {
    fn path(&self) -> Option<&Path>;
}

pub trait FilterClause {
    fn filter(&self) -> Option<&Map<String, Value>>;
}

pub trait EventModesClause {
    fn event_modes(&self) -> Option<&EventModes>;
}

pub trait EventSubscriptionsClause {
    fn event_subscriptions(&self) -> Option<&EventSubscriptions>;
}

pub trait RightsClause {
    fn rights(&self) -> Option<&Rights>;
}

impl CapabilityClause for Capability {
    fn service(&self) -> Option<OneOrMany<Name>> {
        self.service.clone()
    }
    fn protocol(&self) -> Option<OneOrMany<Name>> {
        self.protocol.clone()
    }
    fn directory(&self) -> Option<OneOrMany<Name>> {
        self.directory.as_ref().map(|n| OneOrMany::One(n.clone()))
    }
    fn storage(&self) -> Option<OneOrMany<Name>> {
        self.storage.as_ref().map(|n| OneOrMany::One(n.clone()))
    }
    fn runner(&self) -> Option<OneOrMany<Name>> {
        self.runner.as_ref().map(|n| OneOrMany::One(n.clone()))
    }
    fn resolver(&self) -> Option<OneOrMany<Name>> {
        self.resolver.as_ref().map(|n| OneOrMany::One(n.clone()))
    }
    fn event(&self) -> Option<OneOrMany<Name>> {
        self.event.as_ref().map(|n| OneOrMany::One(n.clone()))
    }
    fn event_stream(&self) -> Option<Name> {
        None
    }
    fn capability_type(&self) -> &'static str {
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
        "capability"
    }
    fn supported(&self) -> &[&'static str] {
        &["service", "protocol", "directory", "storage", "runner", "resolver", "event"]
    }
}

impl AsClause for Capability {
    fn r#as(&self) -> Option<&Name> {
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

impl CapabilityClause for DebugRegistration {
    fn service(&self) -> Option<OneOrMany<Name>> {
        None
    }
    fn protocol(&self) -> Option<OneOrMany<Name>> {
        self.protocol.clone()
    }
    fn directory(&self) -> Option<OneOrMany<Name>> {
        None
    }
    fn storage(&self) -> Option<OneOrMany<Name>> {
        None
    }
    fn runner(&self) -> Option<OneOrMany<Name>> {
        None
    }
    fn resolver(&self) -> Option<OneOrMany<Name>> {
        None
    }
    fn event(&self) -> Option<OneOrMany<Name>> {
        None
    }
    fn event_stream(&self) -> Option<Name> {
        None
    }
    fn capability_type(&self) -> &'static str {
        if self.protocol.is_some() {
            "protocol"
        } else {
            panic!("Missing capability name")
        }
    }
    fn decl_type(&self) -> &'static str {
        "debug"
    }
    fn supported(&self) -> &[&'static str] {
        &["service", "protocol"]
    }
}

impl AsClause for DebugRegistration {
    fn r#as(&self) -> Option<&Name> {
        self.r#as.as_ref()
    }
}

impl PathClause for DebugRegistration {
    fn path(&self) -> Option<&Path> {
        self.path.as_ref()
    }
}

impl FromClause for DebugRegistration {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        OneOrMany::One(AnyRef::from(&self.from))
    }
}

impl CapabilityClause for Use {
    fn service(&self) -> Option<OneOrMany<Name>> {
        self.service.clone()
    }
    fn protocol(&self) -> Option<OneOrMany<Name>> {
        self.protocol.clone()
    }
    fn directory(&self) -> Option<OneOrMany<Name>> {
        self.directory.as_ref().map(|n| OneOrMany::One(n.clone()))
    }
    fn storage(&self) -> Option<OneOrMany<Name>> {
        self.storage.as_ref().map(|n| OneOrMany::One(n.clone()))
    }
    fn runner(&self) -> Option<OneOrMany<Name>> {
        None
    }
    fn resolver(&self) -> Option<OneOrMany<Name>> {
        None
    }
    fn event(&self) -> Option<OneOrMany<Name>> {
        self.event.clone()
    }
    fn event_stream(&self) -> Option<Name> {
        self.event_stream.clone()
    }
    fn capability_type(&self) -> &'static str {
        if self.service.is_some() {
            "service"
        } else if self.protocol.is_some() {
            "protocol"
        } else if self.directory.is_some() {
            "directory"
        } else if self.storage.is_some() {
            "storage"
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
    fn r#as(&self) -> Option<&Name> {
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

impl EventModesClause for Use {
    fn event_modes(&self) -> Option<&EventModes> {
        self.modes.as_ref()
    }
}

impl EventSubscriptionsClause for Use {
    fn event_subscriptions(&self) -> Option<&EventSubscriptions> {
        self.subscriptions.as_ref()
    }
}

impl CapabilityClause for Expose {
    fn service(&self) -> Option<OneOrMany<Name>> {
        self.service.as_ref().map(|n| n.clone())
    }
    fn protocol(&self) -> Option<OneOrMany<Name>> {
        self.protocol.clone()
    }
    fn directory(&self) -> Option<OneOrMany<Name>> {
        self.directory.clone()
    }
    fn storage(&self) -> Option<OneOrMany<Name>> {
        None
    }
    fn runner(&self) -> Option<OneOrMany<Name>> {
        self.runner.clone()
    }
    fn resolver(&self) -> Option<OneOrMany<Name>> {
        self.resolver.clone()
    }
    fn event(&self) -> Option<OneOrMany<Name>> {
        None
    }
    fn event_stream(&self) -> Option<Name> {
        None
    }
    fn capability_type(&self) -> &'static str {
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
    fn r#as(&self) -> Option<&Name> {
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

impl EventModesClause for Expose {
    fn event_modes(&self) -> Option<&EventModes> {
        self.modes.as_ref()
    }
}

impl FromClause for Offer {
    fn from_(&self) -> OneOrMany<AnyRef<'_>> {
        one_or_many_from_impl(&self.from)
    }
}

impl CapabilityClause for Offer {
    fn service(&self) -> Option<OneOrMany<Name>> {
        self.service.as_ref().map(|n| n.clone())
    }
    fn protocol(&self) -> Option<OneOrMany<Name>> {
        self.protocol.clone()
    }
    fn directory(&self) -> Option<OneOrMany<Name>> {
        self.directory.clone()
    }
    fn storage(&self) -> Option<OneOrMany<Name>> {
        self.storage.clone()
    }
    fn runner(&self) -> Option<OneOrMany<Name>> {
        self.runner.clone()
    }
    fn resolver(&self) -> Option<OneOrMany<Name>> {
        self.resolver.clone()
    }
    fn event(&self) -> Option<OneOrMany<Name>> {
        self.event.clone()
    }
    fn event_stream(&self) -> Option<Name> {
        None
    }
    fn capability_type(&self) -> &'static str {
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
    fn r#as(&self) -> Option<&Name> {
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

impl EventModesClause for Offer {
    fn event_modes(&self) -> Option<&EventModes> {
        self.modes.as_ref()
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

pub fn alias_or_name(alias: Option<&Name>, name: &Name) -> Name {
    alias.unwrap_or(name).clone()
}

pub fn alias_or_path(alias: Option<&Path>, path: &Path) -> Path {
    alias.unwrap_or(path).clone()
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        cm_json::{self, Error as JsonError},
        error::Error,
        matches::assert_matches,
        serde_json::{self, json},
        serde_json5,
        std::path::Path,
        test_case::test_case,
    };

    // Exercise reference parsing tests on `OfferFromRef` because it contains every reference
    // subtype.

    #[test]
    fn test_parse_named_reference() {
        assert_matches!("#some-child".parse::<OfferFromRef>(), Ok(OfferFromRef::Named(name)) if name == "some-child");
        assert_matches!("#A".parse::<OfferFromRef>(), Ok(OfferFromRef::Named(name)) if name == "A");
        assert_matches!("#7".parse::<OfferFromRef>(), Ok(OfferFromRef::Named(name)) if name == "7");
        assert_matches!("#_".parse::<OfferFromRef>(), Ok(OfferFromRef::Named(name)) if name == "_");

        assert_matches!("#-".parse::<OfferFromRef>(), Err(_));
        assert_matches!("#.".parse::<OfferFromRef>(), Err(_));
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

    fn parse_as_ref(input: &str) -> Result<OfferFromRef, JsonError> {
        serde_json::from_value::<OfferFromRef>(cm_json::from_json_str(
            input,
            &Path::new("test.cml"),
        )?)
        .map_err(|e| JsonError::parse(format!("{}", e), None, None))
    }

    #[test]
    fn test_deserialize_ref() -> Result<(), JsonError> {
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

    // TODO: Use Default::default() instead

    fn empty_offer() -> Offer {
        Offer {
            service: None,
            protocol: None,
            directory: None,
            storage: None,
            runner: None,
            resolver: None,
            event: None,
            from: OneOrMany::One(OfferFromRef::Self_),
            to: OneOrMany::Many(vec![]),
            r#as: None,
            rights: None,
            subdir: None,
            dependency: None,
            filter: None,
            modes: None,
        }
    }

    fn empty_use() -> Use {
        Use {
            service: None,
            protocol: None,
            directory: None,
            storage: None,
            from: None,
            path: None,
            r#as: None,
            rights: None,
            subdir: None,
            event: None,
            event_stream: None,
            filter: None,
            modes: None,
            subscriptions: None,
            dependency: None,
        }
    }

    #[test]
    fn test_capability_id() -> Result<(), Error> {
        // service
        assert_eq!(
            CapabilityId::from_offer_expose(&Offer {
                service: Some(OneOrMany::One("a".parse().unwrap())),
                ..empty_offer()
            },)?,
            vec![CapabilityId::Service("a".parse().unwrap())]
        );
        assert_eq!(
            CapabilityId::from_offer_expose(&Offer {
                service: Some(OneOrMany::Many(vec!["a".parse().unwrap(), "b".parse().unwrap()],)),
                ..empty_offer()
            },)?,
            vec![
                CapabilityId::Service("a".parse().unwrap()),
                CapabilityId::Service("b".parse().unwrap())
            ]
        );
        assert_eq!(
            CapabilityId::from_use(&Use {
                service: Some(OneOrMany::One("a".parse().unwrap())),
                ..empty_use()
            },)?,
            vec![CapabilityId::UsedService("/svc/a".parse().unwrap())]
        );
        assert_eq!(
            CapabilityId::from_use(&Use {
                service: Some(OneOrMany::Many(vec!["a".parse().unwrap(), "b".parse().unwrap(),],)),
                ..empty_use()
            },)?,
            vec![
                CapabilityId::UsedService("/svc/a".parse().unwrap()),
                CapabilityId::UsedService("/svc/b".parse().unwrap())
            ]
        );
        assert_eq!(
            CapabilityId::from_use(&Use {
                service: Some(OneOrMany::One("a".parse().unwrap())),
                path: Some("/b".parse().unwrap()),
                ..empty_use()
            },)?,
            vec![CapabilityId::UsedService("/b".parse().unwrap())]
        );

        // protocol
        assert_eq!(
            CapabilityId::from_offer_expose(&Offer {
                protocol: Some(OneOrMany::One("a".parse().unwrap())),
                ..empty_offer()
            },)?,
            vec![CapabilityId::Protocol("a".parse().unwrap())]
        );
        assert_eq!(
            CapabilityId::from_offer_expose(&Offer {
                protocol: Some(OneOrMany::Many(vec!["a".parse().unwrap(), "b".parse().unwrap()],)),
                ..empty_offer()
            },)?,
            vec![
                CapabilityId::Protocol("a".parse().unwrap()),
                CapabilityId::Protocol("b".parse().unwrap())
            ]
        );
        assert_eq!(
            CapabilityId::from_use(&Use {
                protocol: Some(OneOrMany::One("a".parse().unwrap())),
                ..empty_use()
            },)?,
            vec![CapabilityId::UsedProtocol("/svc/a".parse().unwrap())]
        );
        assert_eq!(
            CapabilityId::from_use(&Use {
                protocol: Some(OneOrMany::Many(vec!["a".parse().unwrap(), "b".parse().unwrap(),],)),
                ..empty_use()
            },)?,
            vec![
                CapabilityId::UsedProtocol("/svc/a".parse().unwrap()),
                CapabilityId::UsedProtocol("/svc/b".parse().unwrap())
            ]
        );
        assert_eq!(
            CapabilityId::from_use(&Use {
                protocol: Some(OneOrMany::One("a".parse().unwrap())),
                path: Some("/b".parse().unwrap()),
                ..empty_use()
            },)?,
            vec![CapabilityId::UsedProtocol("/b".parse().unwrap())]
        );

        // directory
        assert_eq!(
            CapabilityId::from_offer_expose(&Offer {
                directory: Some(OneOrMany::One("a".parse().unwrap())),
                ..empty_offer()
            },)?,
            vec![CapabilityId::Directory("a".parse().unwrap())]
        );
        assert_eq!(
            CapabilityId::from_offer_expose(&Offer {
                directory: Some(OneOrMany::Many(vec!["a".parse().unwrap(), "b".parse().unwrap()])),
                ..empty_offer()
            },)?,
            vec![
                CapabilityId::Directory("a".parse().unwrap()),
                CapabilityId::Directory("b".parse().unwrap()),
            ]
        );
        assert_eq!(
            CapabilityId::from_use(&Use {
                directory: Some("a".parse().unwrap()),
                path: Some("/b".parse().unwrap()),
                ..empty_use()
            },)?,
            vec![CapabilityId::UsedDirectory("/b".parse().unwrap())]
        );

        // storage
        assert_eq!(
            CapabilityId::from_offer_expose(&Offer {
                storage: Some(OneOrMany::One("cache".parse().unwrap())),
                ..empty_offer()
            },)?,
            vec![CapabilityId::Storage("cache".parse().unwrap())],
        );
        assert_eq!(
            CapabilityId::from_offer_expose(&Offer {
                storage: Some(OneOrMany::Many(vec!["a".parse().unwrap(), "b".parse().unwrap()])),
                ..empty_offer()
            },)?,
            vec![
                CapabilityId::Storage("a".parse().unwrap()),
                CapabilityId::Storage("b".parse().unwrap()),
            ]
        );
        assert_eq!(
            CapabilityId::from_use(&Use {
                storage: Some("a".parse().unwrap()),
                path: Some("/b".parse().unwrap()),
                ..empty_use()
            },)?,
            vec![CapabilityId::UsedStorage("/b".parse().unwrap())]
        );

        // "as" aliasing.
        assert_eq!(
            CapabilityId::from_offer_expose(&Offer {
                service: Some(OneOrMany::One("a".parse().unwrap())),
                r#as: Some("b".parse().unwrap()),
                ..empty_offer()
            },)?,
            vec![CapabilityId::Service("b".parse().unwrap())]
        );

        // Error case.
        assert_matches!(CapabilityId::from_offer_expose(&empty_offer()), Err(_));

        Ok(())
    }

    fn document(contents: serde_json::Value) -> Document {
        serde_json5::from_str::<Document>(&contents.to_string()).unwrap()
    }

    #[test]
    fn test_includes() {
        assert_eq!(document(json!({})).includes(), Vec::<String>::new());
        assert_eq!(document(json!({ "include": []})).includes(), Vec::<String>::new());
        assert_eq!(
            document(json!({ "include": [ "foo.cml", "bar.cml" ]})).includes(),
            vec!["foo.cml", "bar.cml"]
        );
    }

    #[test]
    fn test_merge_same_section() {
        let mut some = document(json!({ "use": [{ "protocol": "foo" }] }));
        let mut other = document(json!({ "use": [{ "protocol": "bar" }] }));
        some.merge_from(&mut other, &Path::new("some/path")).unwrap();
        let uses = some.r#use.as_ref().unwrap();
        assert_eq!(uses.len(), 2);
        assert_eq!(
            uses[0].protocol.as_ref().unwrap(),
            &OneOrMany::One("foo".parse::<Name>().unwrap())
        );
        assert_eq!(
            uses[1].protocol.as_ref().unwrap(),
            &OneOrMany::One("bar".parse::<Name>().unwrap())
        );
    }

    #[test]
    fn test_merge_different_sections() {
        let mut some = document(json!({ "use": [{ "protocol": "foo" }] }));
        let mut other = document(json!({ "expose": [{ "protocol": "bar", "from": "self" }] }));
        some.merge_from(&mut other, &Path::new("some/path")).unwrap();
        let uses = some.r#use.as_ref().unwrap();
        let exposes = some.r#expose.as_ref().unwrap();
        assert_eq!(uses.len(), 1);
        assert_eq!(exposes.len(), 1);
        assert_eq!(
            uses[0].protocol.as_ref().unwrap(),
            &OneOrMany::One("foo".parse::<Name>().unwrap())
        );
        assert_eq!(
            exposes[0].protocol.as_ref().unwrap(),
            &OneOrMany::One("bar".parse::<Name>().unwrap())
        );
    }

    #[test]
    fn test_merge_from_program() {
        let mut some = document(json!({ "program": { "binary": "bin/hello_world" } }));
        let mut other = document(json!({ "program": { "runner": "elf" } }));
        some.merge_from(&mut other, &Path::new("some/path")).unwrap();
        let expected =
            document(json!({ "program": { "binary": "bin/hello_world", "runner": "elf" } }));
        assert_eq!(some.program, expected.program);
    }

    #[test]
    fn test_merge_from_program_without_runner() {
        let mut some =
            document(json!({ "program": { "binary": "bin/hello_world", "runner": "elf" } }));
        // fxbug.dev/79951: merging with a document that doesn't have a runner doesn't override the
        // runner that we already have assigned.
        let mut other = document(json!({ "program": {} }));
        some.merge_from(&mut other, &Path::new("some/path")).unwrap();
        let expected =
            document(json!({ "program": { "binary": "bin/hello_world", "runner": "elf" } }));
        assert_eq!(some.program, expected.program);
    }

    #[test]
    fn test_merge_from_program_overlapping_runner() {
        // It's ok to merge `program.runner = "elf"` with `program.runner = "elf"`.
        let mut some =
            document(json!({ "program": { "binary": "bin/hello_world", "runner": "elf" } }));
        let mut other = document(json!({ "program": { "runner": "elf" } }));
        some.merge_from(&mut other, &Path::new("some/path")).unwrap();
        let expected =
            document(json!({ "program": { "binary": "bin/hello_world", "runner": "elf" } }));
        assert_eq!(some.program, expected.program);
    }

    #[test_case(
        document(json!({ "program": { "runner": "elf" } })),
        document(json!({ "program": { "runner": "fle" } })),
        "runner"
        ; "when_runner_conflicts"
    )]
    #[test_case(
        document(json!({ "program": { "binary": "bin/hello_world" } })),
        document(json!({ "program": { "binary": "bin/hola_mundo" } })),
        "binary"
        ; "when_binary_conflicts"
    )]
    #[test_case(
        document(json!({ "program": { "args": ["a".to_owned()] } })),
        document(json!({ "program": { "args": ["b".to_owned()] } })),
        "args"
        ; "when_args_conflicts"
    )]
    fn test_merge_from_program_error(mut some: Document, mut other: Document, field: &str) {
        matches::assert_matches!(
            some.merge_from(&mut other, &path::Path::new("some/path")),
            Err(Error::Validate { schema_name: None, err, .. })
                if err == format!("manifest include had a conflicting `program.{}`: some/path", field)
        );
    }
}
