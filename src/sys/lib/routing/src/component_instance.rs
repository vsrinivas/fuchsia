// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability_source::{BuiltinCapabilities, NamespaceCapabilities},
        component_id_index::ComponentIdIndex,
        environment::EnvironmentInterface,
        error::ComponentInstanceError,
        policy::GlobalPolicyChecker,
        resolving::{ComponentAddress, ComponentResolutionContext},
        DebugRouteMapper,
    },
    async_trait::async_trait,
    cm_moniker::InstancedAbsoluteMoniker,
    cm_rust::{CapabilityDecl, CollectionDecl, ExposeDecl, OfferDecl, OfferSource, UseDecl},
    derivative::Derivative,
    moniker::{AbsoluteMoniker, ChildMoniker},
    std::{
        clone::Clone,
        sync::{Arc, Weak},
    },
};

/// A trait providing a representation of a component instance.
#[async_trait]
pub trait ComponentInstanceInterface: Sized + Send + Sync {
    type TopInstance: TopInstanceInterface + Send + Sync;
    type DebugRouteMapper: DebugRouteMapper;

    /// Returns a new `WeakComponentInstanceInterface<Self>` pointing to `self`.
    fn as_weak(self: &Arc<Self>) -> WeakComponentInstanceInterface<Self> {
        WeakComponentInstanceInterface::new(self)
    }

    /// Returns this `ComponentInstanceInterface`'s child moniker, if it is
    /// not the root instance.
    fn child_moniker(&self) -> Option<&ChildMoniker>;

    /// Returns this `ComponentInstanceInterface`'s instanced absolute moniker.
    fn instanced_moniker(&self) -> &InstancedAbsoluteMoniker;

    /// Returns this `ComponentInstanceInterface`'s absolute moniker.
    fn abs_moniker(&self) -> &AbsoluteMoniker;

    /// Returns this `ComponentInstanceInterface`'s component URL.
    fn url(&self) -> &str;

    /// Returns a representation of this `ComponentInstanceInterface`'s environment.
    fn environment(&self) -> &dyn EnvironmentInterface<Self>;

    /// Returns the `GlobalPolicyChecker` for this component instance, if it is still available.
    fn try_get_policy_checker(&self) -> Result<GlobalPolicyChecker, ComponentInstanceError>;

    /// Returns the `ComponentIdIndex` available to this component instance, if it is still available.
    fn try_get_component_id_index(&self) -> Result<Arc<ComponentIdIndex>, ComponentInstanceError>;

    /// Gets the parent, if it still exists, or returns an `InstanceNotFound` error.
    fn try_get_parent(&self) -> Result<ExtendedInstanceInterface<Self>, ComponentInstanceError>;

    /// Locks and returns a lazily-resolved and populated
    /// `ResolvedInstanceInterface`.  Returns an `InstanceNotFound` error if the
    /// instance is destroyed. The instance will remain locked until the result
    /// is dropped.
    ///
    /// NOTE: The `Box<dyn>` in the return type is necessary, because the type
    /// of the result depends on the lifetime of the `self` reference. The
    /// proposed "generic associated types" feature would let us define this
    /// statically.
    async fn lock_resolved_state<'a>(
        self: &'a Arc<Self>,
    ) -> Result<Box<dyn ResolvedInstanceInterface<Component = Self> + 'a>, ComponentInstanceError>;

    /// Returns a new mapper for recording a capability route during routing.
    fn new_route_mapper() -> Self::DebugRouteMapper;

    /// Returns the `persistent_storage` setting of this component instance.
    fn persistent_storage(&self) -> bool;
}

/// A trait providing a representation of a resolved component instance.
pub trait ResolvedInstanceInterface: Send + Sync {
    /// Type representing a (unlocked and potentially unresolved) component instance.
    type Component;

    /// Current view of this component's `uses` declarations.
    fn uses(&self) -> Vec<UseDecl>;

    /// Current view of this component's `exposes` declarations.
    fn exposes(&self) -> Vec<ExposeDecl>;

    /// Current view of this component's `offers` declarations.
    fn offers(&self) -> Vec<OfferDecl>;

    /// Current view of this component's `capabilities` declarations.
    fn capabilities(&self) -> Vec<CapabilityDecl>;

    /// Current view of this component's `collections` declarations.
    fn collections(&self) -> Vec<CollectionDecl>;

    /// Returns a live child of this instance.
    fn get_child(&self, moniker: &ChildMoniker) -> Option<Arc<Self::Component>>;

    /// Returns a vector of the live children in `collection`.
    fn children_in_collection(&self, collection: &str)
        -> Vec<(ChildMoniker, Arc<Self::Component>)>;

    /// Returns the resolver-ready location of the component, which is either
    /// an absolute component URL or a relative path URL with context.
    fn address(&self) -> ComponentAddress;

    /// Returns the context to be used to resolve a component from a path
    /// relative to this component (for example, a component in a subpackage).
    /// If `None`, the resolver cannot resolve relative path component URLs.
    fn context_to_resolve_children(&self) -> Option<ComponentResolutionContext>;
}

/// An extension trait providing functionality for any model of a resolved
/// component.
pub trait ResolvedInstanceInterfaceExt: ResolvedInstanceInterface {
    /// Returns true if the given offer source refers to a valid entity, e.g., a
    /// child that exists, a declared collection, etc.
    fn offer_source_exists(&self, source: &OfferSource) -> bool {
        match source {
            OfferSource::Framework | OfferSource::Self_ | OfferSource::Parent => true,
            OfferSource::Void => false,
            OfferSource::Child(cm_rust::ChildRef { name, collection }) => {
                self.get_child(&ChildMoniker::new(name, collection.as_ref())).is_some()
            }
            OfferSource::Collection(collection_name) => {
                self.collections().into_iter().any(|collection| collection.name == *collection_name)
            }
            OfferSource::Capability(capability_name) => self
                .capabilities()
                .into_iter()
                .any(|capability| capability.name() == capability_name),
        }
    }
}

impl<T: ResolvedInstanceInterface> ResolvedInstanceInterfaceExt for T {}

// Elsewhere we need to implement `ResolvedInstanceInterface` for `&T` and
// `MappedMutexGuard<_, _, T>`, where `T : ResolvedComponentInstance`. We can't
// implement the latter outside of this crate because of the "orphan rule". So
// here we implement it for all `Deref`s.
impl<T> ResolvedInstanceInterface for T
where
    T: std::ops::Deref + Send + Sync,
    T::Target: ResolvedInstanceInterface,
{
    type Component = <T::Target as ResolvedInstanceInterface>::Component;

    fn uses(&self) -> Vec<UseDecl> {
        T::Target::uses(&*self)
    }

    fn exposes(&self) -> Vec<ExposeDecl> {
        T::Target::exposes(&*self)
    }

    fn offers(&self) -> Vec<cm_rust::OfferDecl> {
        T::Target::offers(&*self)
    }

    fn capabilities(&self) -> Vec<cm_rust::CapabilityDecl> {
        T::Target::capabilities(&*self)
    }

    fn collections(&self) -> Vec<cm_rust::CollectionDecl> {
        T::Target::collections(&*self)
    }

    fn get_child(&self, moniker: &ChildMoniker) -> Option<Arc<Self::Component>> {
        T::Target::get_child(&*self, moniker)
    }

    fn children_in_collection(
        &self,
        collection: &str,
    ) -> Vec<(ChildMoniker, Arc<Self::Component>)> {
        T::Target::children_in_collection(&*self, collection)
    }

    fn address(&self) -> ComponentAddress {
        T::Target::address(&*self)
    }

    fn context_to_resolve_children(&self) -> Option<ComponentResolutionContext> {
        T::Target::context_to_resolve_children(&*self)
    }
}

/// A wrapper for a weak reference to a type implementing `ComponentInstanceInterface`. Provides the
/// absolute moniker of the component instance, which is useful for error reporting if the original
/// component instance has been destroyed.
#[derive(Derivative)]
#[derivative(Clone(bound = ""), Default(bound = ""), Debug)]
pub struct WeakComponentInstanceInterface<C: ComponentInstanceInterface> {
    #[derivative(Debug = "ignore")]
    inner: Weak<C>,
    pub abs_moniker: AbsoluteMoniker,
}

impl<C: ComponentInstanceInterface> WeakComponentInstanceInterface<C> {
    pub fn new(component: &Arc<C>) -> Self {
        Self { inner: Arc::downgrade(component), abs_moniker: component.abs_moniker().clone() }
    }

    /// Attempts to upgrade this `WeakComponentInterface<C>` into an `Arc<C>`, if the
    /// original component instance interface `C` has not been destroyed.
    pub fn upgrade(&self) -> Result<Arc<C>, ComponentInstanceError> {
        self.inner
            .upgrade()
            .ok_or_else(|| ComponentInstanceError::instance_not_found(self.abs_moniker.clone()))
    }
}

impl<C: ComponentInstanceInterface> From<&Arc<C>> for WeakComponentInstanceInterface<C> {
    fn from(component: &Arc<C>) -> Self {
        Self { inner: Arc::downgrade(component), abs_moniker: component.abs_moniker().clone() }
    }
}

/// Either a type implementing `ComponentInstanceInterface` or its `TopInstance`.
#[derive(Debug, Clone)]
pub enum ExtendedInstanceInterface<C: ComponentInstanceInterface> {
    Component(Arc<C>),
    AboveRoot(Arc<C::TopInstance>),
}

/// A type implementing `ComponentInstanceInterface` or its `TopInstance`, as a weak pointer.
#[derive(Debug)]
pub enum WeakExtendedInstanceInterface<C: ComponentInstanceInterface> {
    Component(WeakComponentInstanceInterface<C>),
    AboveRoot(Weak<C::TopInstance>),
}

impl<C: ComponentInstanceInterface> WeakExtendedInstanceInterface<C> {
    /// Attempts to upgrade this `WeakExtendedInstanceInterface<C>` into an
    /// `ExtendedInstanceInterface<C>`, if the original extended instance has not been destroyed.
    pub fn upgrade(&self) -> Result<ExtendedInstanceInterface<C>, ComponentInstanceError> {
        match self {
            WeakExtendedInstanceInterface::Component(p) => {
                Ok(ExtendedInstanceInterface::Component(p.upgrade()?))
            }
            WeakExtendedInstanceInterface::AboveRoot(p) => {
                Ok(ExtendedInstanceInterface::AboveRoot(
                    p.upgrade().ok_or(ComponentInstanceError::cm_instance_unavailable())?,
                ))
            }
        }
    }
}

impl<C: ComponentInstanceInterface> From<&ExtendedInstanceInterface<C>>
    for WeakExtendedInstanceInterface<C>
{
    fn from(extended: &ExtendedInstanceInterface<C>) -> Self {
        match extended {
            ExtendedInstanceInterface::Component(component) => {
                WeakExtendedInstanceInterface::Component(WeakComponentInstanceInterface::new(
                    component,
                ))
            }
            ExtendedInstanceInterface::AboveRoot(top_instance) => {
                WeakExtendedInstanceInterface::AboveRoot(Arc::downgrade(top_instance))
            }
        }
    }
}

/// A special instance identified with the top of the tree, i.e. component manager's instance.
pub trait TopInstanceInterface: Sized + std::fmt::Debug {
    fn namespace_capabilities(&self) -> &NamespaceCapabilities;

    fn builtin_capabilities(&self) -> &BuiltinCapabilities;
}
