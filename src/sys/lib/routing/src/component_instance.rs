// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::ComponentInstanceError,
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
