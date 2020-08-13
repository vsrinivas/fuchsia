// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io2 as fio2;
use serde::de;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};
use std::collections::HashSet;
use std::fmt;
use std::iter::IntoIterator;
use thiserror::Error;

// Re-export symbols.
pub use cm_types::{
    DependencyType, Durability, Name, NameOrPath, ParseError, Path, RelativePath, StartupMode,
    StorageType, Url, UrlScheme,
};

/// The in-memory representation of a binary Component Manifest JSON file.
/// This has a 1-1 mapping with the FIDL [`ComponentDecl`] table, which has
/// more complete documentation.
///
/// [`ComponentDecl`]: ../../fidl_fuchsia_sys2/struct.ComponentDecl.html
#[derive(Serialize, Deserialize, Debug, Default)]
pub struct Document {
    /// Program information.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub program: Option<Map<String, Value>>,
    /// Used capabilities.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub uses: Option<Vec<Use>>,
    /// Exposed capabilities.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub exposes: Option<Vec<Expose>>,
    /// Offered capabilities.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub offers: Option<Vec<Offer>>,
    /// Capability declarations.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub capabilities: Option<Vec<Capability>>,
    /// Child components.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub children: Option<Vec<Child>>,
    /// Collection declarations.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub collections: Option<Vec<Collection>>,
    /// Freeform object containing third-party metadata.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub facets: Option<Map<String, Value>>,
    // Environment declarations.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub environments: Option<Vec<Environment>>,
}

#[derive(Serialize, Deserialize, Clone, Debug, Eq, PartialEq, Hash)]
#[serde(rename_all = "snake_case")]
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

/// Rights define what permissions are available to exposed, offered or used routes.
#[derive(Serialize, Clone, Debug, PartialEq, Eq)]
pub struct Rights(pub Vec<Right>);

/// A capability declaration. See [`CapabilityDecl`].
///
/// [`CapabilityDecl`]: ../../fidl_fuchsia_sys2/struct.CapabilityDecl.html
#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
pub enum Capability {
    Service(Service),
    Protocol(Protocol),
    Directory(Directory),
    Storage(Storage),
    Runner(Runner),
    Resolver(Resolver),
}

/// A child component. See [`ChildDecl`].
///
/// [`ChildDecl`]: ../../fidl_fuchsia_sys2/struct.ChildDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct Child {
    pub name: Name,
    pub url: Url,
    pub startup: StartupMode,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub environment: Option<Name>,
}

/// A component collection. See [`CollectionDecl`].
///
/// [`CollectionDecl`]: ../../fidl_fuchsia_sys2/struct.CollectionDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct Collection {
    pub name: Name,
    pub durability: Durability,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub environment: Option<Name>,
}

/// A service capability. See [`ServiceDecl`].
///
/// [`ServiceDecl`]: ../../fidl_fuchsia_sys2/struct.ServiceDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct Service {
    pub name: Name,
    pub source_path: Path,
}

/// A protocol capability. See [`ProtocolDecl`].
///
/// [`ProtocolDecl`]: ../../fidl_fuchsia_sys2/struct.ProtocolDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct Protocol {
    pub name: Name,
    pub source_path: Path,
}

/// A directory capability. See [`DirectoryDecl`].
///
/// [`DirectoryDecl`]: ../../fidl_fuchsia_sys2/struct.DirectoryDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct Directory {
    pub name: Name,
    pub source_path: Path,
    pub rights: Rights,
}

/// A storage capability. See [`StorageDecl`].
///
/// [`StorageDecl`]: ../../fidl_fuchsia_sys2/struct.StorageDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct Storage {
    pub name: Name,
    pub source: Ref,
    pub source_path: NameOrPath,
}

/// A runner capability. See [`RunnerDecl`].
///
/// [`RunnerDecl`]: ../../fidl_fuchsia_sys2/struct.RunnerDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct Runner {
    pub name: Name,
    pub source: Ref,
    pub source_path: Path,
}

/// A resolver capability. See [`ResolverDecl`].
///
/// [`ResolverDecl`]: ../../fidl_fuchsia_sys2/struct.ResolverDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct Resolver {
    pub name: Name,
    pub source_path: Path,
}

/// An environment's initial state.
#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(rename_all = "snake_case")]
pub enum EnvironmentExtends {
    // The environment's initial properties are inherited from its realm.
    Realm,
    // The environment does not have any initial properties.
    None,
}

/// An environment capability. See [`EnvironmentDecl`].
///
/// [`EnvironmentDecl`]: ../../fidl_fuchsia_sys2/struct.EnvironmentDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct Environment {
    pub name: Name,
    pub extends: EnvironmentExtends,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub runners: Option<Vec<RunnerRegistration>>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub resolvers: Option<Vec<ResolverRegistration>>,
    #[serde(
        skip_serializing_if = "Option::is_none",
        rename(serialize = "__stop_timeout_ms", deserialize = "__stop_timeout_ms")
    )]
    pub stop_timeout_ms: Option<u32>,
}

/// A registration of a runner capability. See [`RunnerRegistration`].
///
/// [`RunnerRegistration`]: ../../fidl_fuchsia_sys2/struct.RunnerRegistration.html
#[derive(Serialize, Deserialize, Debug)]
pub struct RunnerRegistration {
    pub source_name: Name,
    pub source: Ref,
    pub target_name: Name,
}

/// A registration of a resolver capability to a particular URL scheme. See [`ResolverRegistration`].
///
/// [`ResolverRegistration`]: ../../fidl_fuchsia_sys2/struct.ResolverRegistration.html
#[derive(Serialize, Deserialize, Debug)]
pub struct ResolverRegistration {
    pub resolver: Name,
    pub source: Ref,
    pub scheme: UrlScheme,
}

/// Used capability. See [`UseDecl`].
///
/// [`UseDecl`]: ../../fidl_fuchsia_sys2/enum.UseDecl.html
#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
pub enum Use {
    /// Used service capability.
    Service(UseService),
    /// Used service protocol capability.
    Protocol(UseProtocol),
    /// Used directory capability.
    Directory(UseDirectory),
    /// Used storage capability.
    Storage(UseStorage),
    /// Used runner capability.
    Runner(UseRunner),
    /// Used event capability.
    Event(UseEvent),
    /// Used static event stream capability.
    EventStream(UseEventStream),
}

/// Used service capability. See [`UseServiceDecl`].
///
/// [`UseServiceDecl`]: ../../fidl_fuchsia_sys2/struct.UseServiceDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct UseService {
    /// Used service source.
    pub source: Ref,
    /// Used service source name.
    pub source_name: Name,
    /// Used service target path.
    pub target_path: Path,
}

/// Used service protocol capability. See [`UseProtocolDecl`].
///
/// [`UseProtocolDecl`]: ../../fidl_fuchsia_sys2/struct.UseProtocolDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct UseProtocol {
    /// Used service source.
    pub source: Ref,
    /// Used service source path.
    pub source_path: NameOrPath,
    /// Used service target path.
    pub target_path: NameOrPath,
}

/// Used directory capability. See [`UseDirectoryDecl`].
///
/// [`UseDirectoryDecl`]: ../../fidl_fuchsia_sys2/struct.UseDirectoryDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct UseDirectory {
    /// Used directory source.
    pub source: Ref,
    /// Used directory source path.
    pub source_path: NameOrPath,
    /// Used directory target path.
    pub target_path: NameOrPath,
    /// Used rights for the directory.
    pub rights: Rights,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub subdir: Option<RelativePath>,
}

/// Used storage capability. See [`UseStorageDecl`].
///
/// [`UseStorageDecl`]: ../../fidl_fuchsia_sys2/struct.UseStorageDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct UseStorage {
    /// Used storage type.
    #[serde(rename = "type")]
    pub type_: StorageType,
    /// Used storage target path.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub target_path: Option<Path>,
}

/// Used runner capability. See [`UseRunnerDecl`].
///
/// [`UseRunnerDecl`]: ../../fidl_fuchsia_sys2/struct.UseRunnerDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct UseRunner {
    /// Used runner name.
    pub source_name: Name,
}

/// Used event capability. See [`UseEventDecl`].
///
/// [`UseEventDecl`]: ../../fidl_fuchsia_sys2/struct.UseEventDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct UseEvent {
    /// Used event source.
    pub source: Ref,
    /// Used event source name.
    pub source_name: Name,
    /// Used event target name.
    pub target_name: Name,
    /// Used event filter.
    pub filter: Option<Map<String, Value>>,
}

/// Used event capability. See [`UseEventStreamDecl`].
///
/// [`UseEventStreamDecl`]: ../../fidl_fuchsia_sys2/struct.UseEventStreamDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct UseEventStream {
    /// Used event stream path.
    pub target_path: Path,
    /// Used events.
    pub events: Vec<Name>,
}

/// Exposed capability destination. See [`ExposeTargetDecl`].
///
/// [`ExposeTargetDecl`]: ../../fidl_fuchsia_sys2/enum.ExposeTargetDecl.html
#[derive(Serialize, Deserialize, Clone, Debug)]
#[serde(rename_all = "snake_case")]
pub enum ExposeTarget {
    Parent,
    Framework,
}

/// Exposed capability. See [`ExposeDecl`].
///
/// [`ExposeDecl`]: ../../fidl_fuchsia_sys2/enum.ExposeDecl.html
#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
pub enum Expose {
    Service(ExposeService),
    Protocol(ExposeProtocol),
    Directory(ExposeDirectory),
    Runner(ExposeRunner),
    Resolver(ExposeResolver),
}

/// Exposed service capability. See [`ExposeServiceDecl`].
///
/// [`ExposeServiceDecl`]: ../../fidl_fuchsia_sys2/struct.ExposeServiceDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct ExposeService {
    pub source: Ref,
    pub source_name: Name,
    pub target_name: Name,
    pub target: ExposeTarget,
}

/// Exposed service protocol capability. See [`ExposeProtocolDecl`].
///
/// [`ExposeProtocolDecl`]: ../../fidl_fuchsia_sys2/struct.ExposeProtocolDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct ExposeProtocol {
    pub source: Ref,
    pub source_path: NameOrPath,
    pub target_path: NameOrPath,
    pub target: ExposeTarget,
}

/// Exposed directory capability. See [`ExposeDirectoryDecl`].
///
/// [`ExposeDirectoryDecl`]: ../../fidl_fuchsia_sys2/struct.ExposeDirectoryDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct ExposeDirectory {
    pub source: Ref,
    pub source_path: NameOrPath,
    pub target_path: NameOrPath,
    pub target: ExposeTarget,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub rights: Option<Rights>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub subdir: Option<RelativePath>,
}

/// Exposed runner capability. See [`ExposeRunnerDecl`].
///
/// [`ExposeRunnerDecl`]: ../../fidl_fuchsia_sys2/struct.ExposeRunnerDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct ExposeRunner {
    pub source: Ref,
    pub source_name: Name,
    pub target: ExposeTarget,
    pub target_name: Name,
}

/// Exposed resolver capability. See [`ExposeResolverDecl`].
///
/// [`ExposeResolverDecl`]: ../../fidl_fuchsia_sys2/struct.ExposeResolverDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct ExposeResolver {
    pub source: Ref,
    pub source_name: Name,
    pub target: ExposeTarget,
    pub target_name: Name,
}

/// Offered capability. See [`OfferDecl`].
///
/// [`OfferDecl`]: ../../fidl_fuchsia_sys2/enum.OfferDecl.html
#[derive(Serialize, Deserialize, Debug)]
#[serde(rename_all = "snake_case")]
pub enum Offer {
    Service(OfferService),
    Protocol(OfferProtocol),
    Directory(OfferDirectory),
    Storage(OfferStorage),
    Runner(OfferRunner),
    Resolver(OfferResolver),
    Event(OfferEvent),
}

/// Offered service capability. See [`OfferServiceDecl`].
///
/// [`OfferServiceDecl`]: ../../fidl_fuchsia_sys2/struct.OfferServiceDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct OfferService {
    pub source: Ref,
    pub source_name: Name,
    pub target: Ref,
    pub target_name: Name,
}

/// Offered service protocol capability. See [`OfferProtocolDecl`].
///
/// [`OfferProtocolDecl`]: ../../fidl_fuchsia_sys2/struct.OfferProtocolDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct OfferProtocol {
    pub source: Ref,
    pub source_path: NameOrPath,
    pub target: Ref,
    pub target_path: NameOrPath,
    /// Offered capability dependency_type
    pub dependency_type: DependencyType,
}

/// Offered directory capability. See [`OfferDirectoryDecl`].
///
/// [`OfferDirectoryDecl`]: ../../fidl_fuchsia_sys2/struct.OfferDirectoryDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct OfferDirectory {
    pub source: Ref,
    pub source_path: NameOrPath,
    pub target: Ref,
    pub target_path: NameOrPath,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub rights: Option<Rights>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub subdir: Option<RelativePath>,
    pub dependency_type: DependencyType,
}

/// Offered storage capability. See [`OfferStorageDecl`].
///
/// [`OfferStorageDecl`]: ../../fidl_fuchsia_sys2/struct.OfferStorageDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct OfferStorage {
    #[serde(rename = "type")]
    pub type_: StorageType,
    pub source: Ref,
    pub target: Ref,
}

/// Offered runner capability. See [`OfferRunnerDecl`].
///
/// [`OfferRunnerDecl`]: ../../fidl_fuchsia_sys2/struct.OfferRunnerDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct OfferRunner {
    pub source: Ref,
    pub source_name: Name,
    pub target: Ref,
    pub target_name: Name,
}

/// Offered resolver capability. See [`OfferResolverDecl`].
///
/// [`OfferResolverDecl`]: ../../fidl_fuchsia_sys2/struct.OfferResolverDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct OfferResolver {
    pub source: Ref,
    pub source_name: Name,
    pub target: Ref,
    pub target_name: Name,
}

/// Offered event capability. See [`OfferEventDecl`].
///
/// [`OfferEventDecl`]: ../../fidl_fuchsia_sys2/struct.OfferEventDecl.html
#[derive(Serialize, Deserialize, Debug)]
pub struct OfferEvent {
    pub source: Ref,
    pub source_name: Name,
    pub target: Ref,
    pub target_name: Name,
    pub filter: Option<Map<String, Value>>,
}

/// A reference to a capability relative to this component. See [`Ref`].
///
/// [`Ref`]: ../../fidl_fuchsia_sys2/enum.Ref.html
#[derive(Serialize, Deserialize, Debug, Clone)]
#[serde(rename_all = "snake_case")]
pub enum Ref {
    Parent(ParentRef),
    #[serde(rename = "self")]
    Self_(SelfRef),
    Child(ChildRef),
    Collection(CollectionRef),
    Storage(StorageRef),
    Framework(FrameworkRef),
}

/// A reference to a component's parent. See [`ParentRef`].
///
/// [`ParentRef`]: ../../fidl_fuchsia_sys2/struct.ParentRef.html
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct ParentRef {}

/// A reference to the component itself. See [`SelfRef`].
///
/// [`SelfRef`]: ../../fidl_fuchsia_sys2/struct.SelfRef.html
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct SelfRef {}

/// A reference to one of the component's child instances. See [`ChildRef`].
///
/// [`ChildRef`]: ../../fidl_fuchsia_sys2/struct.ChildRef.html
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct ChildRef {
    pub name: Name,
}

/// A reference to one of the component's collections. See [`CollectionRef`].
///
/// [`CollectionRef`]: ../../fidl_fuchsia_sys2/struct.CollectionRef.html
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct CollectionRef {
    pub name: Name,
}

/// A reference to one of the component's storage sections. See [`StorageRef`].
///
/// [`StorageRef`]: ../../fidl_fuchsia_sys2/struct.StorageRef.html
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct StorageRef {
    pub name: Name,
}

/// A reference to the component framework. See [`FrameworkRef`].
///
/// [`FrameworkRef`]: ../../fidl_fuchsia_sys2/struct.FrameworkRef.html
#[derive(Serialize, Deserialize, Debug, Clone)]
pub struct FrameworkRef {}

/// The error representing a failed validation of a `Path` string.
#[derive(Debug, Error)]
pub enum RightsValidationError {
    /// A right property was provided but was empty. See [`Rights::new`].
    #[error("A right property was provided but was empty.")]
    EmptyRight,
    /// A right was provided which isn't known. See [`Rights::from`].
    #[error("A right was provided which isn't known.")]
    UnknownRight,
    /// A right was provided twice. See [`Rights::new`].
    #[error("A right was duplicated.")]
    DuplicateRight,
}

impl<'de> de::Deserialize<'de> for Rights {
    fn deserialize<D>(deserializer: D) -> Result<Self, D::Error>
    where
        D: de::Deserializer<'de>,
    {
        struct RightsVisitor;
        impl<'de> de::Visitor<'de> for RightsVisitor {
            type Value = Rights;
            fn expecting(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
                f.write_str("an array of strings representing rights with no duplicates.")
            }
            fn visit_seq<S>(self, mut s: S) -> Result<Self::Value, S::Error>
            where
                S: de::SeqAccess<'de>,
            {
                let mut rights_vec = Vec::new();
                while let Some(value) = s.next_element()? {
                    rights_vec.push(value);
                }
                Rights::from(rights_vec).map_err(|err| {
                    let msg = match err {
                        RightsValidationError::EmptyRight => {
                            "a right property was provided but was empty"
                        }
                        RightsValidationError::UnknownRight => {
                            "a right in the string is not recognized"
                        }
                        RightsValidationError::DuplicateRight => {
                            "a right is duplicated in the list"
                        }
                    };
                    de::Error::invalid_value(de::Unexpected::Seq, &msg)
                })
            }
        }
        deserializer.deserialize_seq(RightsVisitor)
    }
}

impl Rights {
    /// Creates a `Rights` from a `Vec<Right>, returning an `Err` if the rights fails validation.
    /// The rights must be non-empty and not contain duplicates.
    pub fn new(rights: Vec<Right>) -> Result<Self, RightsValidationError> {
        if rights.is_empty() {
            return Err(RightsValidationError::EmptyRight);
        }
        let mut seen_rights = HashSet::with_capacity(rights.len());
        for right in rights.iter() {
            if seen_rights.contains(&right) {
                return Err(RightsValidationError::DuplicateRight);
            }
            seen_rights.insert(right);
        }
        Ok(Self(rights))
    }

    /// Creates a `Rights` from a `Vec<String>`, returning an `Err` if the strings fails validation.
    /// The strings must be non-empty and represent valid keywords. Right aliases are noti
    /// accepted at the cm level.
    pub fn from(tokens: Vec<String>) -> Result<Self, RightsValidationError> {
        let mut rights = Vec::<Right>::new();
        for token in tokens.iter() {
            match Rights::map_token(token) {
                Some(right) => {
                    rights.push(right);
                }
                None => return Err(RightsValidationError::UnknownRight),
            }
        }
        Self::new(rights)
    }

    pub fn value(&self) -> &Vec<Right> {
        &self.0
    }

    /// Returns a mapping from a right token string to a Right or None if no mapping can be found.
    pub fn map_token(token: &str) -> Option<Right> {
        match token {
            "admin" => Some(Right::Admin),
            "connect" => Some(Right::Connect),
            "enumerate" => Some(Right::Enumerate),
            "execute" => Some(Right::Execute),
            "get_attributes" => Some(Right::GetAttributes),
            "modify_directory" => Some(Right::ModifyDirectory),
            "read_bytes" => Some(Right::ReadBytes),
            "traverse" => Some(Right::Traverse),
            "update_attributes" => Some(Right::UpdateAttributes),
            "write_bytes" => Some(Right::WriteBytes),
            _ => None,
        }
    }
}

/// Converts a cm::Right into a fil_fuchsia_io2::Operations type using a 1:1 mapping.
impl Into<fio2::Operations> for Right {
    fn into(self) -> fio2::Operations {
        match self {
            Right::Admin => fio2::Operations::Admin,
            Right::Connect => fio2::Operations::Connect,
            Right::Enumerate => fio2::Operations::Enumerate,
            Right::Execute => fio2::Operations::Execute,
            Right::GetAttributes => fio2::Operations::GetAttributes,
            Right::ModifyDirectory => fio2::Operations::ModifyDirectory,
            Right::ReadBytes => fio2::Operations::ReadBytes,
            Right::Traverse => fio2::Operations::Traverse,
            Right::UpdateAttributes => fio2::Operations::UpdateAttributes,
            Right::WriteBytes => fio2::Operations::WriteBytes,
        }
    }
}

impl Into<Option<fio2::Operations>> for Rights {
    fn into(self) -> Option<fio2::Operations> {
        let head = self.value().first();
        if head.is_none() {
            return None;
        }
        let mut operations: fio2::Operations = head.unwrap().clone().into();
        for right in self.into_iter() {
            operations |= right.into();
        }
        Some(operations)
    }
}

impl IntoIterator for Rights {
    type Item = Right;
    type IntoIter = std::vec::IntoIter<Self::Item>;

    fn into_iter(self) -> Self::IntoIter {
        self.0.into_iter()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        serde_json::{self, json},
    };

    macro_rules! expect_ok {
        ($type_:ty, $($input:tt)+) => {
            assert!(serde_json::from_str::<$type_>(
                &json!($($input)*).to_string()).is_ok());
        };
    }

    macro_rules! expect_err {
        ($type_:ty, $($input:tt)+) => {
            assert!(serde_json::from_str::<$type_>(
                &json!($($input)*).to_string()).is_err());
        };
    }

    #[test]
    fn test_valid_rights() {
        expect_ok!(Rights, vec!["read_bytes".to_owned()]);
        expect_ok!(Rights, vec!["write_bytes".to_owned()]);
        expect_ok!(Rights, vec!["admin".to_owned()]);
        expect_ok!(
            Rights,
            vec![
                "admin".to_owned(),
                "connect".to_owned(),
                "enumerate".to_owned(),
                "execute".to_owned(),
                "get_attributes".to_owned(),
                "modify_directory".to_owned(),
                "read_bytes".to_owned(),
                "traverse".to_owned(),
                "update_attributes".to_owned(),
                "write_bytes".to_owned()
            ]
        );
    }

    #[test]
    fn test_invalid_rights() {
        expect_err!(Rights, Vec::<String>::new());
        expect_err!(Rights, vec!["invalid_right".to_owned()]);
        expect_err!(Rights, vec!["read_bytes".to_owned(), "read_bytes".to_owned()]);
    }

    #[test]
    fn test_rights_error_message() {
        let input = r#"
            ["foo"]
        "#;
        let err = serde_json::from_str::<Rights>(input).expect_err("must fail");
        assert_eq!(
            err.to_string(),
            "invalid value: sequence, expected a right in the string is not \
             recognized at line 2 column 19"
        );
        assert_eq!(err.line(), 2);
        assert_eq!(err.column(), 19);
    }
}
