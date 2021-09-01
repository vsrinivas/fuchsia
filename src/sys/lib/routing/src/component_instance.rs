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
    },
    async_trait::async_trait,
    cm_rust::ComponentDecl,
    derivative::Derivative,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, PartialChildMoniker},
    std::{
        clone::Clone,
        sync::{Arc, Weak},
    },
};

/// A trait providing a representation of a component instance.
// TODO(https://fxbug.dev/71901): Add methods providing all the data needed for capability routing.
#[async_trait]
pub trait ComponentInstanceInterface: Sized + Send + Sync {
    type TopInstance: TopInstanceInterface;

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

    /// Returns a representation of this `ComponentInstanceInterface`'s environment.
    fn environment(&self) -> &dyn EnvironmentInterface<Self>;

    /// Returns the `GlobalPolicyChecker` for this component instance, if it is still available.
    fn try_get_policy_checker(&self) -> Result<GlobalPolicyChecker, ComponentInstanceError>;

    /// Returns the `ComponentIdIndex` available to this component instance, if it is still available.
    fn try_get_component_id_index(&self) -> Result<Arc<ComponentIdIndex>, ComponentInstanceError>;

    /// Gets the parent, if it still exists, or returns an `InstanceNotFound` error.
    fn try_get_parent(&self) -> Result<ExtendedInstanceInterface<Self>, ComponentInstanceError>;

    /// Returns a copy of this instance's `ComponentDecl`.
    async fn decl<'a>(self: &'a Arc<Self>) -> Result<ComponentDecl, ComponentInstanceError>;

    /// Returns a live child of this instance.
    async fn get_live_child<'a>(
        self: &'a Arc<Self>,
        moniker: &PartialChildMoniker,
    ) -> Result<Option<Arc<Self>>, ComponentInstanceError>;

    /// Returns a vector of the live children in `collection`.
    async fn live_children_in_collection<'a>(
        self: &'a Arc<Self>,
        collection: &'a str,
    ) -> Result<Vec<(PartialChildMoniker, Arc<Self>)>, ComponentInstanceError>;
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
