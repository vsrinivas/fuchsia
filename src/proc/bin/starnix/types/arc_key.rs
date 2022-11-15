// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ref_cast::RefCast;
use std::borrow::Borrow;
use std::hash::{Hash, Hasher};
use std::ops::Deref;
use std::sync::{Arc, Weak};

/// A wrapper around Arc with Hash implemented based on Arc::as_ptr.
#[derive(RefCast)]
#[repr(transparent)]
pub struct ArcKey<T>(pub Arc<T>);
impl<T> PartialEq for ArcKey<T> {
    fn eq(&self, other: &Self) -> bool {
        Arc::ptr_eq(&self.0, &other.0)
    }
}
impl<T> Eq for ArcKey<T> {}
impl<T> Hash for ArcKey<T> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        Arc::as_ptr(&self.0).hash(state);
    }
}
impl<T> Clone for ArcKey<T> {
    fn clone(&self) -> Self {
        Self(Arc::clone(&self.0))
    }
}
impl<T> Deref for ArcKey<T> {
    type Target = Arc<T>;
    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl<T: std::fmt::Debug> std::fmt::Debug for ArcKey<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.fmt(f)
    }
}

/// A wrapper around Weak with Hash implemented based on Weak::as_ptr.
pub struct WeakKey<T>(pub Weak<T>, PtrKey<T>);
impl<T> WeakKey<T> {
    pub fn from(arc: &Arc<T>) -> Self {
        Self(Arc::downgrade(arc), Arc::as_ptr(arc).into())
    }
}
impl<T> PartialEq for WeakKey<T> {
    fn eq(&self, other: &Self) -> bool {
        Weak::ptr_eq(&self.0, &other.0)
    }
}
impl<T> Eq for WeakKey<T> {}
impl<T> Hash for WeakKey<T> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        Weak::as_ptr(&self.0).hash(state);
    }
}
impl<T> Borrow<PtrKey<T>> for WeakKey<T> {
    fn borrow(&self) -> &PtrKey<T> {
        &self.1
    }
}

pub struct PtrKey<T>(*const T);
impl<T> From<*const T> for PtrKey<T> {
    fn from(ptr: *const T) -> Self {
        Self(ptr)
    }
}
impl<T> PartialEq for PtrKey<T> {
    fn eq(&self, other: &Self) -> bool {
        self.0 == other.0
    }
}
impl<T> Eq for PtrKey<T> {}
impl<T> Hash for PtrKey<T> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.0.hash(state)
    }
}
unsafe impl<T> Sync for PtrKey<T> {}
unsafe impl<T> Send for PtrKey<T> {}
