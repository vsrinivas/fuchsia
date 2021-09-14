// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::writer::{Error, InnerType, State};

/// Trait implemented by properties.
pub trait Property<'t> {
    /// The type of the property.
    type Type;

    /// Set the property value to |value|.
    fn set(&'t self, value: Self::Type);
}

/// Trait implemented by numeric properties providing common operations.
pub trait NumericProperty {
    /// The type the property is handling.
    type Type;

    /// Add the given |value| to the property current value.
    fn add(&self, value: Self::Type);

    /// Subtract the given |value| from the property current value.
    fn subtract(&self, value: Self::Type);

    /// Return the current value of the property for testing.
    /// NOTE: This is a temporary feature to aid unit test of Inspect clients.
    /// It will be replaced by a more comprehensive Read API implementation.
    fn get(&self) -> Result<Self::Type, Error>;
}

/// Trait implemented by all array properties providing common operations on arrays.
pub trait ArrayProperty {
    /// The type of the array entries.
    type Type;

    /// Sets the array value to `value` at the given `index`.
    fn set(&self, index: usize, value: Self::Type);

    /// Adds the given `value` to the property current value at the given `index`.
    fn add(&self, index: usize, value: Self::Type);

    /// Subtracts the given `value` to the property current value at the given `index`.
    fn subtract(&self, index: usize, value: Self::Type);

    /// Sets all slots of the array to 0.
    fn clear(&self);
}

/// Trait implemented by all histogram properties providing common operations.
pub trait HistogramProperty {
    /// The type of each value added to the histogram.
    type Type;

    /// Inserts the given `value` in the histogram.
    fn insert(&self, value: Self::Type);

    /// Inserts the given `value` in the histogram `count` times.
    fn insert_multiple(&self, value: Self::Type, count: usize);

    /// Clears all buckets of the histogram.
    fn clear(&self);
}

#[derive(Default, Debug)]
pub(crate) struct InnerPropertyType;

impl InnerType for InnerPropertyType {
    type Data = ();
    fn free(state: &State, block_index: u32) -> Result<(), Error> {
        let mut state_lock = state.try_lock()?;
        state_lock
            .free_property(block_index)
            .map_err(|err| Error::free("property", block_index, err))
    }
}
