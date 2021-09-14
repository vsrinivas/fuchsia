// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::writer::{Error, State};
use derivative::Derivative;
use std::{
    fmt::Debug,
    sync::{Arc, Weak},
};

/// Trait implemented by all inspect types.
pub trait InspectType: Send + Sync {}

/// Trait implemented by all inspect types. It provides constructor functions that are not
/// intended for use outside the crate.
pub(crate) trait InspectTypeInternal {
    fn new(state: State, block_index: u32) -> Self;
    fn new_no_op() -> Self;
    fn is_valid(&self) -> bool;
}

/// An inner type of all inspect nodes and properties. Each variant implies a
/// different relationship with the underlying inspect VMO.
#[derive(Debug, Derivative)]
#[derivative(Default)]
pub(crate) enum Inner<T: InnerType> {
    /// The node or property is not attached to the inspect VMO.
    #[derivative(Default)]
    None,

    /// The node or property is attached to the inspect VMO, iff its strong
    /// reference is still alive.
    Weak(Weak<InnerRef<T>>),

    /// The node or property is attached to the inspect VMO.
    Strong(Arc<InnerRef<T>>),
}

impl<T: InnerType> Inner<T> {
    /// Creates a new Inner with the desired block index within the inspect VMO
    pub(crate) fn new(state: State, block_index: u32) -> Self {
        Self::Strong(Arc::new(InnerRef { state, block_index, data: T::Data::default() }))
    }

    /// Returns true if the number of strong references to this node or property
    /// is greater than 0.
    pub(crate) fn is_valid(&self) -> bool {
        match self {
            Self::None => false,
            Self::Weak(weak_ref) => weak_ref.strong_count() > 0,
            Self::Strong(_) => true,
        }
    }

    /// Returns a `Some(Arc<InnerRef>)` iff the node or property is currently
    /// attached to inspect, or `None` otherwise. Weak pointers are upgraded
    /// if possible, but their lifetime as strong references are expected to be
    /// short.
    pub(crate) fn inner_ref(&self) -> Option<Arc<InnerRef<T>>> {
        match self {
            Self::None => None,
            Self::Weak(weak_ref) => weak_ref.upgrade(),
            Self::Strong(inner_ref) => Some(Arc::clone(inner_ref)),
        }
    }

    /// Make a weak reference.
    pub(crate) fn clone_weak(&self) -> Self {
        match self {
            Self::None => Self::None,
            Self::Weak(weak_ref) => Self::Weak(weak_ref.clone()),
            Self::Strong(inner_ref) => Self::Weak(Arc::downgrade(inner_ref)),
        }
    }
}

/// Inspect API types implement Eq,PartialEq returning true all the time so that
/// structs embedding inspect types can derive these traits as well.
/// IMPORTANT: Do not rely on these traits implementations for real comparisons
/// or validation tests, instead leverage the reader.
impl<T: InnerType> PartialEq for Inner<T> {
    fn eq(&self, _other: &Self) -> bool {
        true
    }
}

impl<T: InnerType> Eq for Inner<T> {}

/// A type that is owned by inspect nodes and properties, sharing ownership of
/// the inspect VMO heap, and with numerical pointers to the location in the
/// heap in which it resides.
#[derive(Debug)]
pub(crate) struct InnerRef<T: InnerType> {
    /// Index of the block in the VMO.
    pub(crate) block_index: u32,

    /// Reference to the VMO heap.
    pub(crate) state: State,

    /// Associated data for this type.
    pub(crate) data: T::Data,
}

impl<T: InnerType> Drop for InnerRef<T> {
    /// InnerRef has a manual drop impl, to guarantee a single deallocation in
    /// the case of multiple strong references.
    fn drop(&mut self) {
        T::free(&self.state, self.block_index).unwrap();
    }
}

/// De-allocation behavior and associated data for an inner type.
pub(crate) trait InnerType {
    /// Associated data stored on the InnerRef
    type Data: Default + Debug;

    /// De-allocation behavior for when the InnerRef gets dropped
    fn free(state: &State, block_index: u32) -> Result<(), Error>;
}

#[derive(Default, Debug)]
pub(crate) struct InnerValueType;

impl InnerType for InnerValueType {
    type Data = ();
    fn free(state: &State, block_index: u32) -> Result<(), Error> {
        let mut state_lock = state.try_lock()?;
        state_lock.free_value(block_index).map_err(|err| Error::free("value", block_index, err))
    }
}
