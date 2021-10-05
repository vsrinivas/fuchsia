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
        DebugRouteMapper,
    },
    async_trait::async_trait,
    cm_rust::{CapabilityDecl, CollectionDecl, ExposeDecl, OfferDecl, UseDecl},
    derivative::Derivative,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, PartialChildMoniker},
    std::{
        clone::Clone,
        sync::{Arc, Weak},
    },
};

/// A trait providing a representation of a component instance.
#[async_trait]
pub trait ComponentInstanceInterface: Sized + Send + Sync {
    type TopInstance: TopInstanceInterface;
    type DebugRouteMapper: DebugRouteMapper;

    /// Returns a new `WeakComponentInstanceInterface<Self>` pointing to `self`.
    fn as_weak(self: &Arc<Self>) -> WeakComponentInstanceInterface<Self> {
        WeakComponentInstanceInterface::new(self)
    }

    /// Returns this `ComponentInstanceInterface`'s child moniker, if it is not the root instance.
    fn child_moniker(&self) -> Option<&ChildMoniker> {
        self.abs_moniker().leaf()
    }

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
    fn get_live_child(&self, moniker: &PartialChildMoniker) -> Option<Arc<Self::Component>>;

    /// Returns a vector of the live children in `collection`.
    fn live_children_in_collection(
        &self,
        collection: &str,
    ) -> Vec<(PartialChildMoniker, Arc<Self::Component>)>;
}

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

    fn get_live_child(&self, moniker: &PartialChildMoniker) -> Option<Arc<Self::Component>> {
        T::Target::get_live_child(&*self, moniker)
    }

    fn live_children_in_collection(
        &self,
        collection: &str,
    ) -> Vec<(PartialChildMoniker, Arc<Self::Component>)> {
        T::Target::live_children_in_collection(&*self, collection)
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
    pub moniker: AbsoluteMoniker,
}

impl<C: ComponentInstanceInterface> WeakComponentInstanceInterface<C> {
    pub fn new(component: &Arc<C>) -> Self {
        Self { inner: Arc::downgrade(component), moniker: component.abs_moniker().clone() }
    }

    /// Attempts to upgrade this `WeakComponentInterface<C>` into an `Arc<C>`, if the
    /// original component instance interface `C` has not been destroyed.
    pub fn upgrade(&self) -> Result<Arc<C>, ComponentInstanceError> {
        self.inner
            .upgrade()
            .ok_or_else(|| ComponentInstanceError::instance_not_found(self.moniker.to_partial()))
    }
}

impl<C: ComponentInstanceInterface> From<&Arc<C>> for WeakComponentInstanceInterface<C> {
    fn from(component: &Arc<C>) -> Self {
        Self { inner: Arc::downgrade(component), moniker: component.abs_moniker().clone() }
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
