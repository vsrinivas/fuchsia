// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{environment::EnvironmentInterface, error::ComponentInstanceError},
    moniker::{AbsoluteMoniker, ChildMoniker},
    std::fmt,
    std::{
        clone::Clone,
        sync::{Arc, Weak},
    },
};

/// A trait providing a representation of a component instance.
// TODO(https://fxbug.dev/71901): Add methods providing all the data needed for capability routing.
pub trait ComponentInstanceInterface: Sized {
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

    /// Gets the parent, if it still exists, or returns an `InstanceNotFound` error.
    fn try_get_parent(&self) -> Result<ExtendedInstanceInterface<Self>, ComponentInstanceError>;
}

/// A wrapper for a weak reference to a type implementing `ComponentInstanceInterface`. Provides the
/// absolute moniker of the component instance, which is useful for error reporting if the original
/// component instance has been destroyed.
pub struct WeakComponentInstanceInterface<C: ComponentInstanceInterface> {
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
            .ok_or_else(|| ComponentInstanceError::instance_not_found(self.moniker.clone()))
    }
}

impl<C: ComponentInstanceInterface> Clone for WeakComponentInstanceInterface<C> {
    fn clone(&self) -> Self {
        Self { inner: self.inner.clone(), moniker: self.moniker.clone() }
    }
}

impl<C: ComponentInstanceInterface> From<&Arc<C>> for WeakComponentInstanceInterface<C> {
    fn from(component: &Arc<C>) -> Self {
        Self { inner: Arc::downgrade(component), moniker: component.abs_moniker().clone() }
    }
}

impl<C: ComponentInstanceInterface> fmt::Debug for WeakComponentInstanceInterface<C> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("WeakComponentInstanceInterface").field("moniker", &self.moniker).finish()
    }
}

impl<C: ComponentInstanceInterface> Default for WeakComponentInstanceInterface<C> {
    fn default() -> Self {
        WeakComponentInstanceInterface::<C> {
            inner: Weak::new(),
            moniker: AbsoluteMoniker::default(),
        }
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

/// A special instance identified with the top of the tree, i.e. component manager's instance.
pub trait TopInstanceInterface: Sized {}
