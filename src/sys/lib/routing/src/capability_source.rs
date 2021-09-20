// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        component_instance::{
            ComponentInstanceInterface, WeakComponentInstanceInterface,
            WeakExtendedInstanceInterface,
        },
        error::RoutingError,
    },
    async_trait::async_trait,
    cm_rust::{
        CapabilityDecl, CapabilityName, CapabilityPath, CapabilityTypeName, DirectoryDecl,
        EventDecl, ExposeDecl, ExposeDirectoryDecl, ExposeProtocolDecl, ExposeResolverDecl,
        ExposeRunnerDecl, ExposeServiceDecl, ExposeSource, OfferDecl, OfferDirectoryDecl,
        OfferEventDecl, OfferProtocolDecl, OfferResolverDecl, OfferRunnerDecl, OfferServiceDecl,
        OfferSource, OfferStorageDecl, ProtocolDecl, RegistrationSource, ResolverDecl, RunnerDecl,
        ServiceDecl, StorageDecl, UseDecl, UseDirectoryDecl, UseEventDecl, UseProtocolDecl,
        UseServiceDecl, UseSource, UseStorageDecl,
    },
    derivative::Derivative,
    from_enum::FromEnum,
    std::{
        fmt,
        path::PathBuf,
        sync::{Arc, Weak},
    },
    thiserror::Error,
};

#[derive(Debug, Error)]
pub enum Error {
    #[error("Invalid framework capability.")]
    InvalidFrameworkCapability {},
    #[error("Invalid builtin capability.")]
    InvalidBuiltinCapability {},
}

/// Describes the source of a capability, as determined by `find_capability_source`
#[derive(Debug, Derivative)]
#[derivative(Clone(bound = ""))]
pub enum CapabilitySourceInterface<C: ComponentInstanceInterface> {
    /// This capability originates from the component instance for the given Realm.
    /// point.
    Component { capability: ComponentCapability, component: WeakComponentInstanceInterface<C> },
    /// This capability originates from "framework". It's implemented by component manager and is
    /// scoped to the realm of the source.
    Framework { capability: InternalCapability, component: WeakComponentInstanceInterface<C> },
    /// This capability originates from the parent of the root component, and is built in to
    /// component manager. `top_instance` is the instance at the top of the tree, i.e.  the
    /// instance representing component manager.
    Builtin { capability: InternalCapability, top_instance: Weak<C::TopInstance> },
    /// This capability originates from the parent of the root component, and is offered from
    /// component manager's namespace. `top_instance` is the instance at the top of the tree, i.e.
    /// the instance representing component manager.
    Namespace { capability: ComponentCapability, top_instance: Weak<C::TopInstance> },
    /// This capability is provided by the framework based on some other capability.
    Capability {
        source_capability: ComponentCapability,
        component: WeakComponentInstanceInterface<C>,
    },
    /// This capability is an aggregate of capabilities provided by components in a collection.
    Collection {
        collection_name: String,
        source_name: CapabilityName,
        capability_provider: Box<dyn CollectionCapabilityProvider<C>>,
        component: WeakComponentInstanceInterface<C>,
    },
}

impl<C: ComponentInstanceInterface> CapabilitySourceInterface<C> {
    /// Returns whether the given CapabilitySourceInterface can be available in a component's
    /// namespace.
    pub fn can_be_in_namespace(&self) -> bool {
        match self {
            Self::Component { capability, .. } => capability.can_be_in_namespace(),
            Self::Framework { capability, .. } => capability.can_be_in_namespace(),
            Self::Builtin { capability, .. } => capability.can_be_in_namespace(),
            Self::Namespace { capability, .. } => capability.can_be_in_namespace(),
            Self::Capability { .. } => true,
            Self::Collection { .. } => false,
        }
    }

    pub fn source_name(&self) -> Option<&CapabilityName> {
        match self {
            Self::Component { capability, .. } => capability.source_name(),
            Self::Framework { capability, .. } => Some(capability.source_name()),
            Self::Builtin { capability, .. } => Some(capability.source_name()),
            Self::Namespace { capability, .. } => capability.source_name(),
            Self::Capability { .. } => None,
            Self::Collection { source_name, .. } => Some(source_name),
        }
    }

    pub fn type_name(&self) -> CapabilityTypeName {
        match self {
            Self::Component { capability, .. } => capability.type_name(),
            Self::Framework { capability, .. } => capability.type_name(),
            Self::Builtin { capability, .. } => capability.type_name(),
            Self::Namespace { capability, .. } => capability.type_name(),
            Self::Capability { source_capability, .. } => source_capability.type_name(),
            Self::Collection { .. } => CapabilityTypeName::Service,
        }
    }

    pub fn source_instance(&self) -> WeakExtendedInstanceInterface<C> {
        match self {
            Self::Component { component, .. }
            | Self::Framework { component, .. }
            | Self::Capability { component, .. }
            | Self::Collection { component, .. } => {
                WeakExtendedInstanceInterface::Component(component.clone())
            }
            Self::Builtin { top_instance, .. } | Self::Namespace { top_instance, .. } => {
                WeakExtendedInstanceInterface::AboveRoot(top_instance.clone())
            }
        }
    }
}

impl<C: ComponentInstanceInterface> fmt::Display for CapabilitySourceInterface<C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{}",
            match self {
                CapabilitySourceInterface::Component { capability, component } => {
                    format!("{} '{}'", capability, component.moniker)
                }
                CapabilitySourceInterface::Framework { capability, .. } => capability.to_string(),
                CapabilitySourceInterface::Builtin { capability, .. } => capability.to_string(),
                CapabilitySourceInterface::Namespace { capability, .. } => capability.to_string(),
                CapabilitySourceInterface::Capability { source_capability, .. } =>
                    format!("{}", source_capability),
                CapabilitySourceInterface::Collection {
                    collection_name,
                    source_name,
                    component,
                    ..
                } => {
                    format!(
                        "{} '{}' from collection '#{}' of component '{}'",
                        self.type_name(),
                        source_name,
                        collection_name,
                        &component.moniker
                    )
                }
            }
        )
    }
}

/// Information returned by the route_storage_capability function on the source of a storage
/// capability.
#[derive(Debug, Derivative)]
#[derivative(Clone(bound = ""))]
pub struct StorageCapabilitySource<C: ComponentInstanceInterface> {
    /// The component that's providing the backing directory capability for this storage
    /// capability. If None, then the backing directory comes from component_manager's namespace.
    pub storage_provider: Option<Arc<C>>,

    /// The path to the backing directory in the providing component's outgoing directory (or
    /// component_manager's namespace).
    pub backing_directory_path: CapabilityPath,

    /// The subdirectory inside of the backing directory capability to use, if any
    pub backing_directory_subdir: Option<PathBuf>,

    /// The subdirectory inside of the backing directory's sub-directory to use, if any. The
    /// difference between this and backing_directory_subdir is that backing_directory_subdir is
    /// appended to backing_directory_path first, and component_manager will create this subdir if
    /// it doesn't exist but won't create backing_directory_subdir.
    pub storage_subdir: Option<PathBuf>,
}

impl<C: ComponentInstanceInterface> PartialEq for StorageCapabilitySource<C> {
    fn eq(&self, other: &Self) -> bool {
        let self_source_component = self.storage_provider.as_ref().map(|s| s.abs_moniker());
        let other_source_component = other.storage_provider.as_ref().map(|s| s.abs_moniker());
        self_source_component == other_source_component
            && self.backing_directory_path == other.backing_directory_path
            && self.backing_directory_subdir == other.backing_directory_subdir
            && self.storage_subdir == other.storage_subdir
    }
}

/// A provider of a capability whose source originates from zero or more components
/// of a collection. This trait type-erases the capability type, so it can be handled
/// and hosted generically.
#[async_trait]
pub trait CollectionCapabilityProvider<C: ComponentInstanceInterface>: Send + Sync {
    /// Lists the instances of the capability within the collection.
    async fn list_instances(&self) -> Result<Vec<String>, RoutingError>;

    /// Route the capability to its source within the component `instance` of the collection.
    async fn route_instance(
        &self,
        instance: &str,
    ) -> Result<CapabilitySourceInterface<C>, RoutingError>;

    /// Trait-object compatible clone.
    fn clone_boxed(&self) -> Box<dyn CollectionCapabilityProvider<C>>;
}

impl<C: ComponentInstanceInterface> Clone for Box<dyn CollectionCapabilityProvider<C>> {
    fn clone(&self) -> Self {
        self.clone_boxed()
    }
}

impl<C> fmt::Debug for Box<dyn CollectionCapabilityProvider<C>> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Box<dyn CollectionCapabilityProvider>").finish()
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
    Storage(CapabilityName),
}

impl InternalCapability {
    /// Returns whether the given InternalCapability can be available in a component's namespace.
    pub fn can_be_in_namespace(&self) -> bool {
        matches!(
            self,
            InternalCapability::Service(_)
                | InternalCapability::Protocol(_)
                | InternalCapability::Directory(_)
        )
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
            InternalCapability::Storage(_) => CapabilityTypeName::Storage,
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
            InternalCapability::Storage(name) => &name,
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

impl From<ServiceDecl> for InternalCapability {
    fn from(service: ServiceDecl) -> Self {
        Self::Service(service.name)
    }
}

impl From<ProtocolDecl> for InternalCapability {
    fn from(protocol: ProtocolDecl) -> Self {
        Self::Protocol(protocol.name)
    }
}

impl From<DirectoryDecl> for InternalCapability {
    fn from(directory: DirectoryDecl) -> Self {
        Self::Directory(directory.name)
    }
}

impl From<RunnerDecl> for InternalCapability {
    fn from(runner: RunnerDecl) -> Self {
        Self::Runner(runner.name)
    }
}

impl From<ResolverDecl> for InternalCapability {
    fn from(resolver: ResolverDecl) -> Self {
        Self::Resolver(resolver.name)
    }
}

impl From<EventDecl> for InternalCapability {
    fn from(event: EventDecl) -> Self {
        Self::Event(event.name)
    }
}

impl From<StorageDecl> for InternalCapability {
    fn from(storage: StorageDecl) -> Self {
        Self::Storage(storage.name)
    }
}

/// A capability being routed from a component.
#[derive(FromEnum, Clone, Debug, PartialEq, Eq)]
pub enum ComponentCapability {
    Use(UseDecl),
    /// Models a capability used from the environment.
    Environment(EnvironmentCapability),
    Expose(ExposeDecl),
    Offer(OfferDecl),
    Protocol(ProtocolDecl),
    Directory(DirectoryDecl),
    Storage(StorageDecl),
    Runner(RunnerDecl),
    Resolver(ResolverDecl),
    Service(ServiceDecl),
    Event(EventDecl),
}

impl ComponentCapability {
    /// Returns whether the given ComponentCapability can be available in a component's namespace.
    pub fn can_be_in_namespace(&self) -> bool {
        match self {
            ComponentCapability::Use(use_) => {
                matches!(use_, UseDecl::Protocol(_) | UseDecl::Directory(_) | UseDecl::Service(_))
            }
            ComponentCapability::Expose(expose) => matches!(
                expose,
                ExposeDecl::Protocol(_) | ExposeDecl::Directory(_) | ExposeDecl::Service(_)
            ),
            ComponentCapability::Offer(offer) => matches!(
                offer,
                OfferDecl::Protocol(_) | OfferDecl::Directory(_) | OfferDecl::Service(_)
            ),
            ComponentCapability::Protocol(_)
            | ComponentCapability::Directory(_)
            | ComponentCapability::Service(_) => true,
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
                UseDecl::Event(_) => CapabilityTypeName::Event,
                UseDecl::EventStream(_) => CapabilityTypeName::EventStream,
            },
            ComponentCapability::Environment(env) => match env {
                EnvironmentCapability::Runner { .. } => CapabilityTypeName::Runner,
                EnvironmentCapability::Resolver { .. } => CapabilityTypeName::Resolver,
                EnvironmentCapability::Debug { .. } => CapabilityTypeName::Protocol,
            },
            ComponentCapability::Expose(expose) => match expose {
                ExposeDecl::Protocol(_) => CapabilityTypeName::Protocol,
                ExposeDecl::Directory(_) => CapabilityTypeName::Directory,
                ExposeDecl::Service(_) => CapabilityTypeName::Service,
                ExposeDecl::Runner(_) => CapabilityTypeName::Runner,
                ExposeDecl::Resolver(_) => CapabilityTypeName::Resolver,
            },
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
            ComponentCapability::Service(_) => CapabilityTypeName::Service,
            ComponentCapability::Event(_) => CapabilityTypeName::Event,
        }
    }

    /// Return the source path of the capability, if one exists.
    pub fn source_path(&self) -> Option<&CapabilityPath> {
        match self {
            ComponentCapability::Storage(_) => None,
            ComponentCapability::Protocol(protocol) => protocol.source_path.as_ref(),
            ComponentCapability::Directory(directory) => directory.source_path.as_ref(),
            ComponentCapability::Runner(runner) => runner.source_path.as_ref(),
            ComponentCapability::Resolver(resolver) => resolver.source_path.as_ref(),
            ComponentCapability::Service(service) => service.source_path.as_ref(),
            _ => None,
        }
    }

    /// Return the name of the capability, if this is a capability declaration.
    pub fn source_name(&self) -> Option<&CapabilityName> {
        match self {
            ComponentCapability::Storage(storage) => Some(&storage.name),
            ComponentCapability::Protocol(protocol) => Some(&protocol.name),
            ComponentCapability::Directory(directory) => Some(&directory.name),
            ComponentCapability::Runner(runner) => Some(&runner.name),
            ComponentCapability::Resolver(resolver) => Some(&resolver.name),
            ComponentCapability::Service(service) => Some(&service.name),
            ComponentCapability::Event(event) => Some(&event.name),
            ComponentCapability::Use(use_) => match use_ {
                UseDecl::Protocol(UseProtocolDecl { source_name, .. }) => Some(source_name),
                UseDecl::Directory(UseDirectoryDecl { source_name, .. }) => Some(source_name),
                UseDecl::Event(UseEventDecl { source_name, .. }) => Some(source_name),
                UseDecl::Storage(UseStorageDecl { source_name, .. }) => Some(source_name),
                UseDecl::Service(UseServiceDecl { source_name, .. }) => Some(source_name),
                _ => None,
            },
            ComponentCapability::Environment(env_cap) => match env_cap {
                EnvironmentCapability::Runner { source_name, .. } => Some(source_name),
                EnvironmentCapability::Resolver { source_name, .. } => Some(source_name),
                EnvironmentCapability::Debug { source_name, .. } => Some(source_name),
            },
            ComponentCapability::Expose(expose) => match expose {
                ExposeDecl::Protocol(ExposeProtocolDecl { source_name, .. }) => Some(source_name),
                ExposeDecl::Directory(ExposeDirectoryDecl { source_name, .. }) => Some(source_name),
                ExposeDecl::Runner(ExposeRunnerDecl { source_name, .. }) => Some(source_name),
                ExposeDecl::Resolver(ExposeResolverDecl { source_name, .. }) => Some(source_name),
                ExposeDecl::Service(ExposeServiceDecl { source_name, .. }) => Some(source_name),
            },
            ComponentCapability::Offer(offer) => match offer {
                OfferDecl::Protocol(OfferProtocolDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Directory(OfferDirectoryDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Runner(OfferRunnerDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Event(OfferEventDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Storage(OfferStorageDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Resolver(OfferResolverDecl { source_name, .. }) => Some(source_name),
                OfferDecl::Service(OfferServiceDecl { source_name, .. }) => Some(source_name),
            },
        }
    }

    pub fn source_capability_name(&self) -> Option<&CapabilityName> {
        match self {
            ComponentCapability::Offer(OfferDecl::Protocol(OfferProtocolDecl {
                source: OfferSource::Capability(name),
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
}

impl fmt::Display for ComponentCapability {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} '{}' from component", self.type_name(), self.source_id())
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum EnvironmentCapability {
    Runner { source_name: CapabilityName, source: RegistrationSource },
    Resolver { source_name: CapabilityName, source: RegistrationSource },
    Debug { source_name: CapabilityName, source: RegistrationSource },
}

impl EnvironmentCapability {
    pub fn registration_source(&self) -> &RegistrationSource {
        match self {
            Self::Runner { source, .. }
            | Self::Resolver { source, .. }
            | Self::Debug { source, .. } => &source,
        }
    }
}

/// The list of declarations for capabilities from component manager's namespace.
pub type NamespaceCapabilities = Vec<CapabilityDecl>;

/// The list of declarations for capabilities offered by component manager as built-in capabilities.
pub type BuiltinCapabilities = Vec<CapabilityDecl>;

#[cfg(test)]
mod tests {
    use {
        super::*,
        cm_rust::{DependencyType, EventMode, StorageDirectorySource},
        fidl_fuchsia_sys2 as fsys,
    };

    #[test]
    fn capability_type_name() {
        let storage_capability = ComponentCapability::Storage(StorageDecl {
            name: "foo".into(),
            source: StorageDirectorySource::Parent,
            backing_dir: "bar".into(),
            subdir: None,
            storage_id: fsys::StorageId::StaticInstanceIdOrMoniker,
        });
        assert_eq!(storage_capability.type_name(), CapabilityTypeName::Storage);

        let event_capability = ComponentCapability::Use(UseDecl::Event(UseEventDecl {
            dependency_type: DependencyType::Strong,
            source: cm_rust::UseSource::Parent,
            source_name: "started".into(),
            target_name: "started-x".into(),
            filter: None,
            mode: EventMode::Async,
        }));
        assert_eq!(event_capability.type_name(), CapabilityTypeName::Event);
    }
}
