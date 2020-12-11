// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{error::ModelError, realm::WeakRealm},
    async_trait::async_trait,
    cm_rust::*,
    fuchsia_zircon as zx,
    moniker::{AbsoluteMoniker, ChildMoniker},
    std::{collections::HashSet, fmt, path::PathBuf},
    thiserror::Error,
};

#[derive(Debug, Error)]
pub enum Error {
    #[error("Invalid framework capability.")]
    InvalidFrameworkCapability {},
    #[error("Invalid builtin capability.")]
    InvalidBuiltinCapability {},
}

/// The list of declarations for capabilities from component manager's namespace.
pub type NamespaceCapabilities = Vec<cm_rust::CapabilityDecl>;

/// Describes the source of a capability, as determined by `find_capability_source`
#[derive(Clone, Debug)]
pub enum CapabilitySource {
    /// This capability originates from the component instance for the given Realm.
    /// point.
    Component { capability: ComponentCapability, realm: WeakRealm },
    /// This capability originates from "framework". It's implemented by component manager and is
    /// scoped to the realm of the source.
    Framework { capability: InternalCapability, scope_moniker: AbsoluteMoniker },
    /// This capability originates from the containing realm of the root component, and is
    /// built in to component manager.
    Builtin { capability: InternalCapability },
    /// This capability originates from the containing realm of the root component, and is
    /// offered from component manager's namespace.
    Namespace { capability: ComponentCapability },
    /// This capability is provided by the framework based on some other capability.
    Capability { source_capability: ComponentCapability, realm: WeakRealm },
}

impl CapabilitySource {
    /// Returns whether the given CapabilitySource can be available in a component's namespace.
    pub fn can_be_in_namespace(&self) -> bool {
        match self {
            CapabilitySource::Component { capability, .. } => capability.can_be_in_namespace(),
            CapabilitySource::Framework { capability, .. } => capability.can_be_in_namespace(),
            CapabilitySource::Builtin { capability } => capability.can_be_in_namespace(),
            CapabilitySource::Namespace { capability } => capability.can_be_in_namespace(),
            CapabilitySource::Capability { .. } => true,
        }
    }

    pub fn source_name(&self) -> Option<&CapabilityName> {
        match self {
            CapabilitySource::Component { capability, .. } => capability.source_name(),
            CapabilitySource::Framework { capability, .. } => Some(capability.source_name()),
            CapabilitySource::Builtin { capability } => Some(capability.source_name()),
            CapabilitySource::Namespace { capability } => capability.source_name(),
            CapabilitySource::Capability { .. } => None,
        }
    }

    pub fn type_name(&self) -> CapabilityTypeName {
        match self {
            CapabilitySource::Component { capability, .. } => capability.type_name(),
            CapabilitySource::Framework { capability, .. } => capability.type_name(),
            CapabilitySource::Builtin { capability } => capability.type_name(),
            CapabilitySource::Namespace { capability } => capability.type_name(),
            CapabilitySource::Capability { source_capability, .. } => source_capability.type_name(),
        }
    }
}

impl fmt::Display for CapabilitySource {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                CapabilitySource::Component { capability, realm } => {
                    format!("{} '{}'", capability, realm.moniker)
                }
                CapabilitySource::Framework { capability, .. } => capability.to_string(),
                CapabilitySource::Builtin { capability } => capability.to_string(),
                CapabilitySource::Namespace { capability } => capability.to_string(),
                CapabilitySource::Capability { source_capability, .. } =>
                    format!("{}", source_capability),
            }
        )
    }
}

/// Describes a capability provided by the component manager which could be a framework capability
/// scoped to a realm, a built-in global capability, or a capability from component manager's own
/// namespace.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum InternalCapability {
    Service(CapabilityName),
    Protocol(CapabilityName),
    Directory(CapabilityName),
    Runner(CapabilityName),
    Event(CapabilityName),
    Resolver(CapabilityName),
}

impl InternalCapability {
    /// Returns whether the given InternalCapability can be available in a component's namespace.
    pub fn can_be_in_namespace(&self) -> bool {
        matches!(self, InternalCapability::Service(_) |
                       InternalCapability::Protocol(_) |
                       InternalCapability::Directory(_))
    }

    /// Returns a name for the capability type.
    pub fn type_name(&self) -> CapabilityTypeName {
        match self {
            InternalCapability::Service(_) => CapabilityTypeName::Service,
            InternalCapability::Protocol(_) => CapabilityTypeName::Protocol,
            InternalCapability::Directory(_) => CapabilityTypeName::Directory,
            InternalCapability::Runner(_) => CapabilityTypeName::Runner,
            InternalCapability::Event(_) => CapabilityTypeName::Event,
            InternalCapability::Resolver(_) => CapabilityTypeName::Resolver,
        }
    }

    pub fn source_name(&self) -> &CapabilityName {
        match self {
            InternalCapability::Service(name) => &name,
            InternalCapability::Protocol(name) => &name,
            InternalCapability::Directory(name) => &name,
            InternalCapability::Runner(name) => &name,
            InternalCapability::Event(name) => &name,
            InternalCapability::Resolver(name) => &name,
        }
    }

    pub fn builtin_from_use_decl(decl: &UseDecl) -> Result<Self, Error> {
        match decl {
            UseDecl::Service(s) if s.source == UseSource::Parent => {
                Ok(InternalCapability::Service(s.source_name.clone()))
            }
            UseDecl::Protocol(s) if s.source == UseSource::Parent => {
                Ok(InternalCapability::Protocol(s.source_name.clone()))
            }
            UseDecl::Directory(d) if d.source == UseSource::Parent => {
                Ok(InternalCapability::Directory(d.source_name.clone()))
            }
            UseDecl::Event(e) if e.source == UseSource::Parent => {
                Ok(InternalCapability::Event(e.source_name.clone()))
            }
            UseDecl::Runner(s) => Ok(InternalCapability::Runner(s.source_name.clone())),
            _ => Err(Error::InvalidBuiltinCapability {}),
        }
    }

    pub fn builtin_from_offer_decl(decl: &OfferDecl) -> Result<Self, Error> {
        match decl {
            OfferDecl::Protocol(s) if s.source == OfferServiceSource::Parent => {
                Ok(InternalCapability::Protocol(s.source_name.clone()))
            }
            OfferDecl::Directory(d) if d.source == OfferDirectorySource::Parent => {
                Ok(InternalCapability::Directory(d.source_name.clone()))
            }
            OfferDecl::Runner(s) if s.source == OfferRunnerSource::Parent => {
                Ok(InternalCapability::Runner(s.source_name.clone()))
            }
            OfferDecl::Event(e) if e.source == OfferEventSource::Parent => {
                Ok(InternalCapability::Event(e.source_name.clone()))
            }
            OfferDecl::Resolver(r) if r.source == OfferResolverSource::Parent => {
                Ok(InternalCapability::Resolver(r.source_name.clone()))
            }
            _ => {
                return Err(Error::InvalidBuiltinCapability {});
            }
        }
    }

    pub fn builtin_from_storage_decl(decl: &StorageDecl) -> Result<Self, Error> {
        if decl.source == StorageDirectorySource::Parent {
            Ok(InternalCapability::Directory(decl.backing_dir.clone()))
        } else {
            Err(Error::InvalidBuiltinCapability {})
        }
    }

    pub fn framework_from_use_decl(decl: &UseDecl) -> Result<Self, Error> {
        match decl {
            UseDecl::Service(s) if s.source == UseSource::Framework => {
                Ok(InternalCapability::Service(s.source_name.clone()))
            }
            UseDecl::Protocol(s) if s.source == UseSource::Framework => {
                Ok(InternalCapability::Protocol(s.source_name.clone()))
            }
            UseDecl::Directory(d) if d.source == UseSource::Framework => {
                Ok(InternalCapability::Directory(d.source_name.clone()))
            }
            UseDecl::Event(e) if e.source == UseSource::Framework => {
                Ok(InternalCapability::Event(e.source_name.clone()))
            }
            _ => {
                return Err(Error::InvalidFrameworkCapability {});
            }
        }
    }

    pub fn framework_from_offer_decl(decl: &OfferDecl) -> Result<Self, Error> {
        match decl {
            OfferDecl::Protocol(s) if s.source == OfferServiceSource::Parent => {
                Ok(InternalCapability::Protocol(s.source_name.clone()))
            }
            OfferDecl::Directory(d) if d.source == OfferDirectorySource::Framework => {
                Ok(InternalCapability::Directory(d.source_name.clone()))
            }
            OfferDecl::Event(e) if e.source == OfferEventSource::Framework => {
                Ok(InternalCapability::Event(e.source_name.clone()))
            }
            _ => {
                return Err(Error::InvalidFrameworkCapability {});
            }
        }
    }

    pub fn framework_from_expose_decl(decl: &ExposeDecl) -> Result<Self, Error> {
        match decl {
            ExposeDecl::Protocol(s) if s.source == ExposeSource::Framework => {
                Ok(InternalCapability::Protocol(s.source_name.clone()))
            }
            ExposeDecl::Directory(d) if d.source == ExposeSource::Framework => {
                Ok(InternalCapability::Directory(d.source_name.clone()))
            }
            _ => {
                return Err(Error::InvalidFrameworkCapability {});
            }
        }
    }

    /// Returns true if this is a protocol with name that matches `name`.
    pub fn matches_protocol(&self, name: &CapabilityName) -> bool {
        match self {
            Self::Protocol(source_name) => source_name == name,
            _ => false,
        }
    }
}

impl fmt::Display for InternalCapability {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} '{}' from component manager", self.type_name(), self.source_name())
    }
}

/// The server-side of a capability implements this trait.
/// Multiple `CapabilityProvider` objects can compose with one another for a single
/// capability request. For example, a `CapabitilityProvider` can be interposed
/// between the primary `CapabilityProvider and the client for the purpose of
/// logging and testing. A `CapabilityProvider` is typically provided by a
/// corresponding `Hook` in response to the `CapabilityRouted` event.
/// A capability provider is used exactly once as a result of exactly one route.
#[async_trait]
pub trait CapabilityProvider: Send + Sync {
    // Called to bind a server end of a zx::Channel to the provided capability.
    // If the capability is a directory, then |flags|, |open_mode| and |relative_path|
    // will be propagated along to open the appropriate directory.
    async fn open(
        self: Box<Self>,
        flags: u32,
        open_mode: u32,
        relative_path: PathBuf,
        server_end: &mut zx::Channel,
    ) -> Result<(), ModelError>;
}

/// A capability being routed from a component.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum ComponentCapability {
    Use(UseDecl),
    /// Models a capability used from the environment.
    Environment(EnvironmentCapability),
    Expose(ExposeDecl),
    /// Models a capability hosted from the exposed dir which is used at runtime.
    UsedExpose(ExposeDecl),
    Offer(OfferDecl),
    Protocol(ProtocolDecl),
    Directory(DirectoryDecl),
    Storage(StorageDecl),
    Runner(RunnerDecl),
    Resolver(ResolverDecl),
}

impl ComponentCapability {
    /// Returns whether the given ComponentCapability can be available in a component's namespace.
    pub fn can_be_in_namespace(&self) -> bool {
        match self {
            ComponentCapability::Use(use_) => matches!(use_, UseDecl::Protocol(_) |
                               UseDecl::Directory(_) |
                               UseDecl::Service(_)),
            ComponentCapability::Expose(expose) | ComponentCapability::UsedExpose(expose) => {
                matches!(expose, ExposeDecl::Protocol(_) |
                                 ExposeDecl::Directory(_) |
                                 ExposeDecl::Service(_))
            }
            ComponentCapability::Offer(offer) => matches!(offer, OfferDecl::Protocol(_) |
                                OfferDecl::Directory(_) |
                                OfferDecl::Service(_)),
            ComponentCapability::Protocol(_) | ComponentCapability::Directory(_) => true,
            _ => false,
        }
    }

    /// Returns a name for the capability type.
    pub fn type_name(&self) -> CapabilityTypeName {
        match self {
            ComponentCapability::Use(use_) => match use_ {
                UseDecl::Protocol(_) => CapabilityTypeName::Protocol,
                UseDecl::Directory(_) => CapabilityTypeName::Directory,
                UseDecl::Service(_) => CapabilityTypeName::Service,
                UseDecl::Storage(_) => CapabilityTypeName::Storage,
                UseDecl::Runner(_) => CapabilityTypeName::Runner,
                UseDecl::Event(_) => CapabilityTypeName::Event,
                UseDecl::EventStream(_) => CapabilityTypeName::EventStream,
            },
            ComponentCapability::Environment(env) => match env {
                EnvironmentCapability::Runner { .. } => CapabilityTypeName::Runner,
                EnvironmentCapability::Resolver { .. } => CapabilityTypeName::Resolver,
            },
            ComponentCapability::Expose(expose) | ComponentCapability::UsedExpose(expose) => {
                match expose {
                    ExposeDecl::Protocol(_) => CapabilityTypeName::Protocol,
                    ExposeDecl::Directory(_) => CapabilityTypeName::Directory,
                    ExposeDecl::Service(_) => CapabilityTypeName::Service,
                    ExposeDecl::Runner(_) => CapabilityTypeName::Runner,
                    ExposeDecl::Resolver(_) => CapabilityTypeName::Resolver,
                }
            }
            ComponentCapability::Offer(offer) => match offer {
                OfferDecl::Protocol(_) => CapabilityTypeName::Protocol,
                OfferDecl::Directory(_) => CapabilityTypeName::Directory,
                OfferDecl::Service(_) => CapabilityTypeName::Service,
                OfferDecl::Storage(_) => CapabilityTypeName::Storage,
                OfferDecl::Runner(_) => CapabilityTypeName::Runner,
                OfferDecl::Resolver(_) => CapabilityTypeName::Resolver,
                OfferDecl::Event(_) => CapabilityTypeName::Event,
            },
            ComponentCapability::Protocol(_) => CapabilityTypeName::Protocol,
            ComponentCapability::Directory(_) => CapabilityTypeName::Directory,
            ComponentCapability::Storage(_) => CapabilityTypeName::Storage,
            ComponentCapability::Runner(_) => CapabilityTypeName::Runner,
            ComponentCapability::Resolver(_) => CapabilityTypeName::Resolver,
        }
    }

    /// Return the source path of the capability, if one exists.
    pub fn source_path(&self) -> Option<&CapabilityPath> {
        match self {
            ComponentCapability::Storage(_) => None,
            ComponentCapability::Protocol(protocol) => Some(&protocol.source_path),
            ComponentCapability::Directory(directory) => Some(&directory.source_path),
            ComponentCapability::Runner(runner) => Some(&runner.source_path),
            ComponentCapability::Resolver(resolver) => Some(&resolver.source_path),
            _ => None,
        }
    }

    /// Return the name of the capability, if this is a capability declaration.
    pub fn source_name<'a>(&self) -> Option<&CapabilityName> {
        match self {
            ComponentCapability::Storage(storage) => Some(&storage.name),
            ComponentCapability::Protocol(protocol) => Some(&protocol.name),
            ComponentCapability::Directory(directory) => Some(&directory.name),
            ComponentCapability::Runner(runner) => Some(&runner.name),
            ComponentCapability::Resolver(resolver) => Some(&resolver.name),
            ComponentCapability::Use(use_) => match use_ {
                UseDecl::Protocol(UseProtocolDecl { source_name, .. }) => Some(source_name),
                UseDecl::Directory(UseDirectoryDecl { source_name, .. }) => Some(source_name),
                UseDecl::Runner(UseRunnerDecl { source_name, .. }) => Some(source_name),
                UseDecl::Event(UseEventDecl { source_name, .. }) => Some(source_name),
                UseDecl::Storage(UseStorageDecl { source_name, .. }) => Some(source_name),
                _ => None,
            },
            ComponentCapability::Environment(env_cap) => match env_cap {
                EnvironmentCapability::Runner { source_name, .. } => Some(source_name),
                EnvironmentCapability::Resolver { source_name, .. } => Some(source_name),
            },
            ComponentCapability::Expose(expose) => match expose {
                ExposeDecl::Protocol(ExposeProtocolDecl { source_name, .. }) => Some(source_name),
                ExposeDecl::Directory(ExposeDirectoryDecl { source_name, .. }) => Some(source_name),
                ExposeDecl::Runner(ExposeRunnerDecl { source_name, .. }) => Some(source_name),
                ExposeDecl::Resolver(ExposeResolverDecl { source_name, .. }) => Some(source_name),
                _ => None,
            },
            ComponentCapability::UsedExpose(expose) => {
                // A UsedExpose needs to be matched to the ExposeDecl the UsedExpose wraps at the
                // same component. This is accomplished by returning the ExposeDecl's target path.
                // Effectively, it's as if the UsedExposed were a UseDecl with both the source and
                // target name equal to `target_name`.
                match expose {
                    ExposeDecl::Protocol(ExposeProtocolDecl { target_name, .. }) => {
                        Some(target_name)
                    }
                    ExposeDecl::Directory(ExposeDirectoryDecl { target_name, .. }) => {
                        Some(target_name)
                    }
                    _ => None,
                }
            }
            ComponentCapability::Offer(offer) => match offer {
                OfferDecl::Protocol(OfferProtocolDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Directory(OfferDirectoryDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Runner(OfferRunnerDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Event(OfferEventDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Storage(OfferStorageDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Resolver(OfferResolverDecl { source_name, .. }) => Some(source_name),
                _ => None,
            },
        }
    }

    pub fn source_capability_name<'a>(&self) -> Option<&CapabilityName> {
        match self {
            ComponentCapability::Offer(OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferServiceSource::Capability(name),
                ..
            })) => Some(name),
            ComponentCapability::Expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                source: ExposeSource::Capability(name),
                ..
            })) => Some(name),
            ComponentCapability::Use(UseDecl::Protocol(UseProtocolDecl {
                source: UseSource::Capability(name),
                ..
            })) => Some(name),
            _ => None,
        }
    }

    /// Returns the source path or name of the capability as a string, useful for debugging.
    pub fn source_id(&self) -> String {
        self.source_name()
            .map(|p| format!("{}", p))
            .or_else(|| self.source_path().map(|p| format!("{}", p)))
            .unwrap_or_default()
    }

    /// Returns the `ExposeDecl` that exposes the capability, if it exists.
    pub fn find_expose_source<'a>(&self, decl: &'a ComponentDecl) -> Option<&'a ExposeDecl> {
        decl.exposes.iter().find(|&expose| match (self, expose) {
            // Protocol exposed to me that has a matching `expose` or `offer`.
            (
                ComponentCapability::Offer(OfferDecl::Protocol(parent_offer)),
                ExposeDecl::Protocol(expose),
            ) => parent_offer.source_name == expose.target_name,
            (
                ComponentCapability::Expose(ExposeDecl::Protocol(parent_expose)),
                ExposeDecl::Protocol(expose),
            ) => parent_expose.source_name == expose.target_name,
            (
                ComponentCapability::UsedExpose(ExposeDecl::Protocol(used_expose)),
                ExposeDecl::Protocol(expose),
            ) => used_expose.target_name == expose.target_name,
            // Directory exposed to me that matches a directory `expose` or `offer`.
            (
                ComponentCapability::Offer(OfferDecl::Directory(parent_offer)),
                ExposeDecl::Directory(expose),
            ) => parent_offer.source_name == expose.target_name,
            (
                ComponentCapability::Expose(ExposeDecl::Directory(parent_expose)),
                ExposeDecl::Directory(expose),
            ) => parent_expose.source_name == expose.target_name,
            (
                ComponentCapability::UsedExpose(ExposeDecl::Directory(used_expose)),
                ExposeDecl::Directory(expose),
            ) => used_expose.target_name == expose.target_name,
            // Runner exposed to me that has a matching `expose` or `offer`.
            (
                ComponentCapability::Offer(OfferDecl::Runner(parent_offer)),
                ExposeDecl::Runner(expose),
            ) => parent_offer.source_name == expose.target_name,
            (
                ComponentCapability::Expose(ExposeDecl::Runner(parent_expose)),
                ExposeDecl::Runner(expose),
            ) => parent_expose.source_name == expose.target_name,
            (
                ComponentCapability::Environment(EnvironmentCapability::Runner {
                    source_name, ..
                }),
                ExposeDecl::Runner(expose),
            ) => source_name == &expose.target_name,
            // Resolver exposed to me that has a matching `expose` or `offer`.
            (
                ComponentCapability::Offer(OfferDecl::Resolver(parent_offer)),
                ExposeDecl::Resolver(expose),
            ) => parent_offer.source_name == expose.target_name,
            (
                ComponentCapability::Expose(ExposeDecl::Resolver(parent_expose)),
                ExposeDecl::Resolver(expose),
            ) => parent_expose.source_name == expose.target_name,
            (
                ComponentCapability::Environment(EnvironmentCapability::Resolver {
                    source_name,
                    ..
                }),
                ExposeDecl::Resolver(expose),
            ) => source_name == &expose.target_name,
            // Directory exposed to me that matches a `storage` declaration which consumes it.
            (ComponentCapability::Storage(parent_storage), ExposeDecl::Directory(expose)) => {
                parent_storage.backing_dir == expose.target_name
            }
            _ => false,
        })
    }

    /// Returns the set of `ExposeServiceDecl`s that expose the service capability, if they exist.
    #[allow(unused)]
    pub fn find_expose_service_sources<'a>(
        &self,
        decl: &'a ComponentDecl,
    ) -> Vec<&'a ExposeServiceDecl> {
        let names: HashSet<_> = match self {
            ComponentCapability::Offer(OfferDecl::Service(parent_offer)) => {
                parent_offer.sources.iter().map(|s| &s.source_name).collect()
            }
            ComponentCapability::Expose(ExposeDecl::Service(parent_expose)) => {
                parent_expose.sources.iter().map(|s| &s.source_name).collect()
            }
            _ => panic!("Expected an offer or expose of a service capability, found: {:?}", self),
        };
        decl.exposes
            .iter()
            .filter_map(|expose| match expose {
                ExposeDecl::Service(expose) if names.contains(&expose.target_name) => Some(expose),
                _ => None,
            })
            .collect()
    }

    /// Given a parent ComponentDecl, returns the `OfferDecl` that offers this capability to
    /// `child_moniker`, if it exists.
    pub fn find_offer_source<'a>(
        &self,
        decl: &'a ComponentDecl,
        child_moniker: &ChildMoniker,
    ) -> Option<&'a OfferDecl> {
        decl.offers.iter().find(|&offer| {
            match (self, offer) {
                // Protocol offered to me that matches a service `use` or `offer` declaration.
                (
                    ComponentCapability::Use(UseDecl::Protocol(child_use)),
                    OfferDecl::Protocol(offer),
                ) => Self::is_offer_protocol_or_directory_match(
                    child_moniker,
                    &child_use.source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                (
                    ComponentCapability::Offer(OfferDecl::Protocol(child_offer)),
                    OfferDecl::Protocol(offer),
                ) => Self::is_offer_protocol_or_directory_match(
                    child_moniker,
                    &child_offer.source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                // Directory offered to me that matches a directory `use` or `offer` declaration.
                (
                    ComponentCapability::Use(UseDecl::Directory(child_use)),
                    OfferDecl::Directory(offer),
                ) => Self::is_offer_protocol_or_directory_match(
                    child_moniker,
                    &child_use.source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                (
                    ComponentCapability::Offer(OfferDecl::Directory(child_offer)),
                    OfferDecl::Directory(offer),
                ) => Self::is_offer_protocol_or_directory_match(
                    child_moniker,
                    &child_offer.source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                // Directory offered to me that matches a `storage` declaration which consumes it.
                (ComponentCapability::Storage(child_storage), OfferDecl::Directory(offer)) => {
                    Self::is_offer_protocol_or_directory_match(
                        child_moniker,
                        &child_storage.backing_dir,
                        &offer.target,
                        &offer.target_name,
                    )
                }
                // Storage offered to me.
                (
                    ComponentCapability::Use(UseDecl::Storage(child_use)),
                    OfferDecl::Storage(offer),
                ) => Self::is_offer_storage_match(
                    child_moniker,
                    &child_use.source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                (
                    ComponentCapability::Offer(OfferDecl::Storage(child_offer)),
                    OfferDecl::Storage(offer),
                ) => Self::is_offer_storage_match(
                    child_moniker,
                    &child_offer.source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                // Runners offered from parent.
                (
                    ComponentCapability::Use(UseDecl::Runner(child_use)),
                    OfferDecl::Runner(offer),
                ) => Self::is_offer_runner_resolver_or_event_match(
                    child_moniker,
                    &child_use.source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                (
                    ComponentCapability::Offer(OfferDecl::Runner(child_offer)),
                    OfferDecl::Runner(offer),
                ) => Self::is_offer_runner_resolver_or_event_match(
                    child_moniker,
                    &child_offer.source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                (
                    ComponentCapability::Environment(EnvironmentCapability::Runner {
                        source_name,
                        ..
                    }),
                    OfferDecl::Runner(offer),
                ) => Self::is_offer_runner_resolver_or_event_match(
                    child_moniker,
                    &source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                // Resolvers offered from parent.
                (
                    ComponentCapability::Offer(OfferDecl::Resolver(child_offer)),
                    OfferDecl::Resolver(offer),
                ) => Self::is_offer_runner_resolver_or_event_match(
                    child_moniker,
                    &child_offer.source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                (
                    ComponentCapability::Environment(EnvironmentCapability::Resolver {
                        source_name,
                        ..
                    }),
                    OfferDecl::Resolver(offer),
                ) => Self::is_offer_runner_resolver_or_event_match(
                    child_moniker,
                    source_name,
                    &offer.target,
                    &offer.target_name,
                ),
                // Events offered from parent.
                (ComponentCapability::Use(UseDecl::Event(child_use)), OfferDecl::Event(offer)) => {
                    Self::is_offer_runner_resolver_or_event_match(
                        child_moniker,
                        &child_use.source_name,
                        &offer.target,
                        &offer.target_name,
                    )
                }
                (
                    ComponentCapability::Offer(OfferDecl::Event(child_offer)),
                    OfferDecl::Event(parent_offer),
                ) => Self::is_offer_runner_resolver_or_event_match(
                    child_moniker,
                    &child_offer.source_name,
                    &parent_offer.target,
                    &parent_offer.target_name,
                ),
                _ => false,
            }
        })
    }

    /// Given a list of namespace capabilities, returns the one that matches `self`, if any.
    pub fn find_namespace_source<'a>(
        &self,
        namespace_capabilities: &'a Vec<CapabilityDecl>,
    ) -> Option<&'a CapabilityDecl> {
        namespace_capabilities.iter().find(|&capability| match (self, capability) {
            (
                ComponentCapability::Use(UseDecl::Protocol(child_use)),
                CapabilityDecl::Protocol(p),
            ) => child_use.source_name == p.name,
            (
                ComponentCapability::Offer(OfferDecl::Protocol(child_offer)),
                CapabilityDecl::Protocol(p),
            ) => child_offer.source_name == p.name,
            (
                ComponentCapability::Use(UseDecl::Directory(child_use)),
                CapabilityDecl::Directory(d),
            ) => child_use.source_name == d.name,
            (
                ComponentCapability::Offer(OfferDecl::Directory(child_offer)),
                CapabilityDecl::Directory(d),
            ) => child_offer.source_name == d.name,
            (ComponentCapability::Storage(child_storage), CapabilityDecl::Directory(d)) => {
                child_storage.backing_dir == d.name
            }
            _ => false,
        })
    }

    /// Returns the set of `OfferServiceDecl`s that offer the service capability, if they exist.
    #[allow(unused)]
    pub fn find_offer_service_sources<'a>(
        &self,
        decl: &'a ComponentDecl,
        child_moniker: &ChildMoniker,
    ) -> Vec<&'a OfferServiceDecl> {
        let names: HashSet<_> = match self {
            ComponentCapability::Use(UseDecl::Service(child_use)) => {
                vec![&child_use.source_name].into_iter().collect()
            }
            ComponentCapability::Offer(OfferDecl::Service(child_offer)) => {
                child_offer.sources.iter().map(|s| &s.source_name).collect()
            }
            _ => panic!("Expected a use or offer of a service capability, found: {:?}", self),
        };
        decl.offers
            .iter()
            .filter_map(|offer| match offer {
                OfferDecl::Service(offer)
                    if Self::is_offer_service_match(
                        child_moniker,
                        &names,
                        &offer.target,
                        &offer.target_name,
                    ) =>
                {
                    Some(offer)
                }
                _ => None,
            })
            .collect()
    }

    /// Given an offer/expose of a protocol from `self`, return the associated ProtocolDecl,
    /// if it exists.
    pub fn find_protocol_source<'a>(&self, decl: &'a ComponentDecl) -> Option<&'a ProtocolDecl> {
        decl.find_protocol_source(self.source_name()?)
    }

    /// Given an offer/expose of a directory from `self` or a storage decl, return the associated DirectoryDecl,
    /// if it exists.
    pub fn find_directory_source<'a>(&self, decl: &'a ComponentDecl) -> Option<&'a DirectoryDecl> {
        match &self {
            ComponentCapability::Storage(storage) => {
                decl.find_directory_source(&storage.backing_dir)
            }
            _ => decl.find_directory_source(self.source_name()?),
        }
    }

    /// Given an offer/expose of a runner from `self`, return the associated RunnerDecl,
    /// if it exists.
    pub fn find_runner_source<'a>(&self, decl: &'a ComponentDecl) -> Option<&'a RunnerDecl> {
        decl.find_runner_source(self.source_name()?)
    }

    /// Given an offer/expose of a resolver from `self`, return the associated ResolverDecl,
    /// if it exists.
    pub fn find_resolver_source<'a>(&self, decl: &'a ComponentDecl) -> Option<&'a ResolverDecl> {
        decl.find_resolver_source(self.source_name()?)
    }

    /// Given a name of a storage from `self`, return the associated StorageDecl,
    /// if it exists.
    pub fn find_storage_source<'a>(&self, decl: &'a ComponentDecl) -> Option<&'a StorageDecl> {
        decl.find_storage_source(self.source_capability_name()?)
    }

    fn is_offer_service_match(
        child_moniker: &ChildMoniker,
        names: &HashSet<&CapabilityName>,
        target: &OfferTarget,
        target_name: &CapabilityName,
    ) -> bool {
        names.contains(target_name) && target_matches_moniker(target, child_moniker)
    }

    fn is_offer_protocol_or_directory_match(
        child_moniker: &ChildMoniker,
        name: &CapabilityName,
        target: &OfferTarget,
        target_name: &CapabilityName,
    ) -> bool {
        name == target_name && target_matches_moniker(target, child_moniker)
    }

    fn is_offer_storage_match(
        child_moniker: &ChildMoniker,
        child_name: &CapabilityName,
        parent_target: &OfferTarget,
        parent_name: &CapabilityName,
    ) -> bool {
        // The names must match...
        parent_name == child_name &&
        // ...and the child/collection names must match.
        target_matches_moniker(parent_target, child_moniker)
    }

    fn is_offer_runner_resolver_or_event_match(
        child_moniker: &ChildMoniker,
        source_name: &CapabilityName,
        target: &OfferTarget,
        target_name: &CapabilityName,
    ) -> bool {
        source_name == target_name && target_matches_moniker(target, child_moniker)
    }
}

impl fmt::Display for ComponentCapability {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} '{}' from component", self.type_name(), self.source_id())
    }
}

/// Returns if `parent_target` refers to a the child `child_moniker`.
fn target_matches_moniker(parent_target: &OfferTarget, child_moniker: &ChildMoniker) -> bool {
    match (parent_target, child_moniker.collection()) {
        (OfferTarget::Child(target_child_name), None) => target_child_name == child_moniker.name(),
        (OfferTarget::Collection(target_collection_name), Some(collection)) => {
            target_collection_name == collection
        }
        _ => false,
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EnvironmentCapability {
    Runner { source_name: CapabilityName, source: RegistrationSource },
    Resolver { source_name: CapabilityName, source: RegistrationSource },
}

impl EnvironmentCapability {
    pub fn registration_source(&self) -> &RegistrationSource {
        match self {
            Self::Runner { source, .. } | Self::Resolver { source, .. } => &source,
        }
    }
}

#[cfg(test)]
mod tests {
    use cm_rust::OfferServiceDecl;
    use {
        super::*,
        crate::model::testing::test_helpers::default_component_decl,
        cm_rust::{
            ExposeRunnerDecl, ExposeSource, ExposeTarget, OfferServiceSource, ServiceSource,
            StorageDirectorySource, UseRunnerDecl,
        },
    };

    #[test]
    fn find_expose_service_sources() {
        let capability = ComponentCapability::Expose(ExposeDecl::Service(ExposeServiceDecl {
            sources: vec![
                ServiceSource { source: ExposeServiceSource::Self_, source_name: "net".into() },
                ServiceSource { source: ExposeServiceSource::Self_, source_name: "log".into() },
                ServiceSource {
                    source: ExposeServiceSource::Self_,
                    source_name: "unmatched-source".into(),
                },
            ],
            target: ExposeTarget::Parent,
            target_name: "".into(),
        }));
        let net_service = ExposeServiceDecl {
            sources: vec![],
            target: ExposeTarget::Parent,
            target_name: "net".into(),
        };
        let log_service = ExposeServiceDecl {
            sources: vec![],
            target: ExposeTarget::Parent,
            target_name: "log".into(),
        };
        let unmatched_service = ExposeServiceDecl {
            sources: vec![],
            target: ExposeTarget::Parent,
            target_name: "unmatched-target".into(),
        };
        let decl = ComponentDecl {
            exposes: vec![
                ExposeDecl::Service(net_service.clone()),
                ExposeDecl::Service(log_service.clone()),
                ExposeDecl::Service(unmatched_service.clone()),
            ],
            ..default_component_decl()
        };
        let sources = capability.find_expose_service_sources(&decl);
        assert_eq!(sources, vec![&net_service, &log_service])
    }

    #[test]
    #[should_panic]
    #[ignore] // fxbug.dev/40189
    fn find_expose_service_sources_with_unexpected_capability() {
        let capability = ComponentCapability::Storage(StorageDecl {
            name: "".into(),
            source: StorageDirectorySource::Parent,
            backing_dir: "".into(),
            subdir: None,
        });
        capability.find_expose_service_sources(&default_component_decl());
    }

    #[test]
    fn find_offer_service_sources() {
        let capability = ComponentCapability::Offer(OfferDecl::Service(OfferServiceDecl {
            sources: vec![
                ServiceSource { source: OfferServiceSource::Self_, source_name: "net".into() },
                ServiceSource { source: OfferServiceSource::Self_, source_name: "log".into() },
                ServiceSource {
                    source: OfferServiceSource::Self_,
                    source_name: "unmatched-source".into(),
                },
            ],
            target: OfferTarget::Child("".to_string()),
            target_name: "".into(),
        }));
        let net_service = OfferServiceDecl {
            sources: vec![],
            target: OfferTarget::Child("child".to_string()),
            target_name: "net".into(),
        };
        let log_service = OfferServiceDecl {
            sources: vec![],
            target: OfferTarget::Child("child".to_string()),
            target_name: "log".into(),
        };
        let unmatched_service = OfferServiceDecl {
            sources: vec![],
            target: OfferTarget::Child("child".to_string()),
            target_name: "unmatched-target".into(),
        };
        let decl = ComponentDecl {
            offers: vec![
                OfferDecl::Service(net_service.clone()),
                OfferDecl::Service(log_service.clone()),
                OfferDecl::Service(unmatched_service.clone()),
            ],
            ..default_component_decl()
        };
        let moniker = ChildMoniker::new("child".to_string(), None, 0);
        let sources = capability.find_offer_service_sources(&decl, &moniker);
        assert_eq!(sources, vec![&net_service, &log_service])
    }

    #[test]
    fn find_offer_source_runner() {
        // Parent offers runner named "elf" to "child".
        let parent_decl = ComponentDecl {
            offers: vec![
                // Offer as "elf" to chilr_d "child".
                OfferDecl::Runner(cm_rust::OfferRunnerDecl {
                    source: cm_rust::OfferRunnerSource::Self_,
                    source_name: "source".into(),
                    target: cm_rust::OfferTarget::Child("child".to_string()),
                    target_name: "elf".into(),
                }),
            ],
            ..default_component_decl()
        };

        // A child named "child" uses a runner "elf" offered by its parent. Should successfully
        // match the declaration.
        let child_cap =
            ComponentCapability::Use(UseDecl::Runner(UseRunnerDecl { source_name: "elf".into() }));
        assert_eq!(
            child_cap.find_offer_source(&parent_decl, &"child:0".into()),
            Some(&parent_decl.offers[0])
        );

        // Mismatched child name.
        assert_eq!(child_cap.find_offer_source(&parent_decl, &"other-child:0".into()), None);

        // Mismatched cap name.
        let misnamed_child_cap = ComponentCapability::Use(UseDecl::Runner(UseRunnerDecl {
            source_name: "dwarf".into(),
        }));
        assert_eq!(misnamed_child_cap.find_offer_source(&parent_decl, &"child:0".into()), None);
    }

    #[test]
    fn find_offer_source_storage() {
        // Parent offers storage named "cache" to "child".
        let parent_decl = ComponentDecl {
            offers: vec![
                // Offer as "cache" to child "child".
                OfferDecl::Storage(cm_rust::OfferStorageDecl {
                    source: cm_rust::OfferStorageSource::Self_,
                    source_name: "source".into(),
                    target: cm_rust::OfferTarget::Child("child".to_string()),
                    target_name: "cache".into(),
                }),
            ],
            ..default_component_decl()
        };

        // A child named "child" uses storage "cache" offered by its parent. Should successfully
        // match the declaration.
        let child_cap = ComponentCapability::Use(UseDecl::Storage(UseStorageDecl {
            source_name: "cache".into(),
            target_path: "/target-path".parse().unwrap(),
        }));
        assert_eq!(
            child_cap.find_offer_source(&parent_decl, &"child:0".into()),
            Some(&parent_decl.offers[0])
        );

        // Mismatched child name.
        assert_eq!(child_cap.find_offer_source(&parent_decl, &"other-child:0".into()), None);

        // Mismatched cap name.
        let misnamed_child_cap = ComponentCapability::Use(UseDecl::Storage(UseStorageDecl {
            source_name: "not-cache".into(),
            target_path: "/target-path".parse().unwrap(),
        }));
        assert_eq!(misnamed_child_cap.find_offer_source(&parent_decl, &"child:0".into()), None);
    }

    #[test]
    fn find_offer_source_event() {
        // Parent offers event named "started" to "child"
        let parent_decl = ComponentDecl {
            offers: vec![OfferDecl::Event(cm_rust::OfferEventDecl {
                source: cm_rust::OfferEventSource::Parent,
                source_name: "source".into(),
                target: cm_rust::OfferTarget::Child("child".to_string()),
                target_name: "started".into(),
                filter: None,
            })],
            ..default_component_decl()
        };

        // A child named "child" uses the event "started" offered by its parent. Should
        // successfully match the declaration.
        let child_cap = ComponentCapability::Use(UseDecl::Event(UseEventDecl {
            source: cm_rust::UseSource::Parent,
            source_name: "started".into(),
            target_name: "started-x".into(),
            filter: None,
        }));

        assert_eq!(
            child_cap.find_offer_source(&parent_decl, &"child:0".into()),
            Some(&parent_decl.offers[0])
        );

        // Mismatched child name.
        assert_eq!(child_cap.find_offer_source(&parent_decl, &"other-child:0".into()), None);

        // Mismatched capability name.
        let misnamed_child_cap = ComponentCapability::Use(UseDecl::Event(UseEventDecl {
            source: cm_rust::UseSource::Parent,
            source_name: "foo".into(),
            target_name: "started".into(),
            filter: None,
        }));
        assert_eq!(misnamed_child_cap.find_offer_source(&parent_decl, &"child:0".into()), None);
    }

    #[test]
    fn find_expose_source_runner() {
        // A child named "child" exposes a runner "elf" to its parent.
        let child_decl = ComponentDecl {
            exposes: vec![
                // Expose as "elf" to Realm.
                ExposeDecl::Runner(cm_rust::ExposeRunnerDecl {
                    source: cm_rust::ExposeSource::Self_,
                    source_name: "source".into(),
                    target: cm_rust::ExposeTarget::Parent,
                    target_name: "elf".into(),
                }),
            ],
            ..default_component_decl()
        };

        // A parent exposes a runner "elf" with a child as its source. Should successfully match the
        // declaration.
        let parent_cap = ComponentCapability::Expose(ExposeDecl::Runner(ExposeRunnerDecl {
            source: ExposeSource::Child("child".into()),
            source_name: "elf".into(),
            target: ExposeTarget::Parent,
            target_name: "parent_elf".into(),
        }));
        assert_eq!(parent_cap.find_expose_source(&child_decl), Some(&child_decl.exposes[0]));

        // If the name is mismatched, we shouldn't find anything though.
        let misnamed_parent_cap =
            ComponentCapability::Expose(ExposeDecl::Runner(ExposeRunnerDecl {
                source: ExposeSource::Child("child".into()),
                source_name: "dwarf".into(),
                target: ExposeTarget::Parent,
                target_name: "parent_elf".into(),
            }));
        assert_eq!(misnamed_parent_cap.find_expose_source(&child_decl), None);
    }

    #[test]
    #[should_panic]
    #[ignore] // fxbug.dev/40189
    fn find_offer_service_sources_with_unexpected_capability() {
        let capability = ComponentCapability::Storage(StorageDecl {
            name: "".into(),
            source: StorageDirectorySource::Parent,
            backing_dir: "".into(),
            subdir: None,
        });
        let moniker = ChildMoniker::new("".to_string(), None, 0);
        capability.find_offer_service_sources(&default_component_decl(), &moniker);
    }

    #[test]
    fn capability_type_name() {
        let storage_capability = ComponentCapability::Storage(StorageDecl {
            name: "foo".into(),
            source: StorageDirectorySource::Parent,
            backing_dir: "bar".into(),
            subdir: None,
        });
        assert_eq!(storage_capability.type_name(), CapabilityTypeName::Storage);

        let event_capability = ComponentCapability::Use(UseDecl::Event(UseEventDecl {
            source: cm_rust::UseSource::Parent,
            source_name: "started".into(),
            target_name: "started-x".into(),
            filter: None,
        }));
        assert_eq!(event_capability.type_name(), CapabilityTypeName::Event);
    }
}
