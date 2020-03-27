// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate supplies the `Inspect` trait and procedural macro, which enables certain types
//! to be inspectable within Fuchsia.

use fuchsia_inspect::{
    BoolProperty, BytesProperty, DoubleProperty, IntProperty, Node, Property, StringProperty,
    UintProperty,
};

// Re-export the Inspect macro. Users should depend directly on this crate.
pub use fuchsia_inspect_derive_macro::Inspect;

/// A data structure that can be inspected with `fuchsia_inspect`.
pub trait Inspect {
    /// This associated type owns a subtree (either a property or a node) of a parent inspect node.
    /// May be nested. When dropped, the subtree is detached from the parent.
    type Data;

    /// Insert an inspect subtree at `parent[name]` with values from `self` and return
    /// the inspect data.
    fn inspect_create<T: AsRef<str>>(&self, parent: &Node, name: T) -> Self::Data;

    /// Update the inspect subtree owned by the inspect data with values from self.
    fn inspect_update(&self, data: &mut Self::Data);
}

impl Inspect for String {
    type Data = StringProperty;

    fn inspect_create<T: AsRef<str>>(&self, parent: &Node, name: T) -> Self::Data {
        parent.create_string(name, self)
    }

    fn inspect_update(&self, data: &mut Self::Data) {
        data.set(self);
    }
}

impl Inspect for Vec<u8> {
    type Data = BytesProperty;

    fn inspect_create<T: AsRef<str>>(&self, parent: &Node, name: T) -> Self::Data {
        parent.create_bytes(name, &self)
    }

    fn inspect_update(&self, data: &mut Self::Data) {
        data.set(&self);
    }
}

/// Implement `Inspect` for a primitive type. Some implementations result in a
/// non-lossy upcast in order to conform to the supported types in the inspect API.
///   `impl_t`: The primitive types to be implemented, e.g. `{ u8, u16 }`
///   `inspect_t`: The type the inspect API expects, e.g. `u64`
///   `prop_name`: The name the inspect API uses for functions, e.g. `uint`
///   `prop_name_cap`: The name the inspect API uses for types, e.g. `Uint`
macro_rules! impl_inspect_primitive {
    ({ $($impl_t:ty), *}, $inspect_t:ty, $prop_name:ident, $prop_name_cap:ident) => {
        $(
            paste::item! {
                impl Inspect for $impl_t {
                    type Data = [<$prop_name_cap Property>];

                    fn inspect_create<T: AsRef<str>>(&self, parent: &Node, name: T) -> Self::Data {
                        parent.[<create_ $prop_name>](name, *self as $inspect_t)
                    }

                    fn inspect_update(&self, data: &mut Self::Data) {
                        data.set(*self as $inspect_t);
                    }
                }
            }
        )*
    };
}

// Implement `Inspect` for the supported primitive types.
impl_inspect_primitive!({ u8, u16, u32, u64, usize }, u64, uint, Uint);
impl_inspect_primitive!({ i8, i16, i32, i64 }, i64, int, Int);
impl_inspect_primitive!({ f32, f64 }, f64, double, Double);
impl_inspect_primitive!({ bool }, bool, bool, Bool);
