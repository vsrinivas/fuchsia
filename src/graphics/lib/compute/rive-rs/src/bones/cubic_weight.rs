// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cell::Cell;

use crate::{
    bones::Weight,
    core::{Core, ObjectRef, OnAdded, Property},
    math,
};

#[derive(Debug)]
pub struct CubicWeight {
    weight: Weight,
    in_values: Property<u64>,
    in_indices: Property<u64>,
    out_values: Property<u64>,
    out_indices: Property<u64>,
    in_translation: Cell<math::Vec>,
    out_translation: Cell<math::Vec>,
}

impl ObjectRef<'_, CubicWeight> {
    pub fn in_values(&self) -> u64 {
        self.in_values.get()
    }

    pub fn set_in_values(&self, in_values: u64) {
        self.in_values.set(in_values);
    }

    pub fn in_indices(&self) -> u64 {
        self.in_indices.get()
    }

    pub fn set_in_indices(&self, in_indices: u64) {
        self.in_indices.set(in_indices);
    }

    pub fn out_values(&self) -> u64 {
        self.out_values.get()
    }

    pub fn set_out_values(&self, out_values: u64) {
        self.out_values.set(out_values);
    }

    pub fn out_indices(&self) -> u64 {
        self.out_indices.get()
    }

    pub fn set_out_indices(&self, out_indices: u64) {
        self.out_indices.set(out_indices);
    }
}

impl ObjectRef<'_, CubicWeight> {
    pub fn in_translation(&self) -> math::Vec {
        self.in_translation.get()
    }

    pub fn set_in_translation(&self, in_translation: math::Vec) {
        self.in_translation.set(in_translation);
    }

    pub fn out_translation(&self) -> math::Vec {
        self.out_translation.get()
    }

    pub fn set_out_translation(&self, out_translation: math::Vec) {
        self.out_translation.set(out_translation);
    }
}

impl Core for CubicWeight {
    parent_types![(weight, Weight)];

    properties![
        (110, in_values, set_in_values),
        (111, in_indices, set_in_indices),
        (112, out_values, set_out_values),
        (113, out_indices, set_out_indices),
        weight,
    ];
}

impl OnAdded for ObjectRef<'_, CubicWeight> {
    on_added!(Weight);
}

impl Default for CubicWeight {
    fn default() -> Self {
        Self {
            weight: Weight::default(),
            in_values: Property::new(255),
            in_indices: Property::new(1),
            out_values: Property::new(255),
            out_indices: Property::new(1),
            in_translation: Cell::new(math::Vec::default()),
            out_translation: Cell::new(math::Vec::default()),
        }
    }
}
