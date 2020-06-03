// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate provides types, traits and macros for ergonomic
//! interactions with `fuchsia_inspect`. Proc macros are originally defined
//! in a separate crate, but re-exported here. Users should depend directly
//! on this crate.

mod inspect;

use core::fmt;
use core::ops::{Deref, DerefMut};
use fuchsia_inspect::{
    BoolProperty, BytesProperty, DoubleProperty, IntProperty, Node, Property, StringProperty,
    UintProperty,
};
pub use inspect::{AttachError, Inspect, WithInspect};
use std::marker::PhantomData;

/// Re-export Node, used by the procedural macros in order to get a canonical,
/// stable import path. User code does not need `fuchsia_inspect` in their
/// namespace.
#[doc(hidden)]
pub use fuchsia_inspect::Node as InspectNode;

/// The `Unit` derive macro can be applied to named structs in order to generate an
/// implementation of the `Unit` trait. The name of the field corresponds to the
/// inspect node or property name, and the type of the field must also implement `Unit`.
/// Implementations of `Unit` are supplied for most primitives and `String`.
///
/// Example:
///
/// #[derive(Unit)]
/// struct Point {
///     x: f32,
///     y: f32,
/// }
pub use fuchsia_inspect_derive_macro::{Inspect, Unit};

/// Provides a custom inspect `fuchsia_inspect` subtree for a type which is
/// created, updated and removed in a single step. (It does NOT support per-field updates.)
pub trait Unit {
    /// This associated type owns a subtree (either a property or a node) of a parent inspect node.
    /// May be nested. When dropped, the subtree is detached from the parent.
    /// Default is required such that a detached state can be constructed. The base inspect node
    /// and property types implement default.
    type Data: Default;

    /// Insert an inspect subtree at `parent[name]` with values from `self` and return
    /// the inspect data.
    fn inspect_create(&self, parent: &Node, name: impl AsRef<str>) -> Self::Data;

    /// Update the inspect subtree owned by the inspect data with values from self.
    fn inspect_update(&self, data: &mut Self::Data);
}

impl Unit for String {
    type Data = StringProperty;

    fn inspect_create(&self, parent: &Node, name: impl AsRef<str>) -> Self::Data {
        parent.create_string(name, self)
    }

    fn inspect_update(&self, data: &mut Self::Data) {
        data.set(self);
    }
}

impl Unit for Vec<u8> {
    type Data = BytesProperty;

    fn inspect_create(&self, parent: &Node, name: impl AsRef<str>) -> Self::Data {
        parent.create_bytes(name, &self)
    }

    fn inspect_update(&self, data: &mut Self::Data) {
        data.set(&self);
    }
}

/// Implement `Unit` for a primitive type. Some implementations result in a
/// non-lossy upcast in order to conform to the supported types in the inspect API.
///   `impl_t`: The primitive types to be implemented, e.g. `{ u8, u16 }`
///   `inspect_t`: The type the inspect API expects, e.g. `u64`
///   `prop_name`: The name the inspect API uses for functions, e.g. `uint`
///   `prop_name_cap`: The name the inspect API uses for types, e.g. `Uint`
macro_rules! impl_unit_primitive {
    ({ $($impl_t:ty), *}, $inspect_t:ty, $prop_name:ident, $prop_name_cap:ident) => {
        $(
            paste::item! {
                impl Unit for $impl_t {
                    type Data = [<$prop_name_cap Property>];

                    fn inspect_create(&self, parent: &Node, name: impl AsRef<str>) -> Self::Data {
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

// Implement `Unit` for the supported primitive types.
impl_unit_primitive!({ u8, u16, u32, u64, usize }, u64, uint, Uint);
impl_unit_primitive!({ i8, i16, i32, i64, isize }, i64, int, Int);
impl_unit_primitive!({ f32, f64 }, f64, double, Double);
impl_unit_primitive!({ bool }, bool, bool, Bool);

/// The inspect data of an Option<T> gets the same inspect representation as T,
/// but can also be absent.
pub struct OptionData<T: Unit> {
    // Keep a copy of the owned name, so that the inner node or property can be
    // reinitialized after initial attachment.
    name: String,

    // Keep a reference to the parent, so that the inner node or property can be
    // reinitialized after initial attachment.
    inspect_parent: Node,

    // Inner inspect data.
    inspect_data: Option<T::Data>,
}

impl<T: Unit> Default for OptionData<T> {
    fn default() -> Self {
        Self { name: String::default(), inspect_parent: Node::default(), inspect_data: None }
    }
}

impl<T: Unit> Unit for Option<T> {
    type Data = OptionData<T>;

    fn inspect_create(&self, parent: &Node, name: impl AsRef<str>) -> Self::Data {
        Self::Data {
            name: String::from(name.as_ref()),
            inspect_parent: parent.clone_weak(),
            inspect_data: self.as_ref().map(|inner| inner.inspect_create(&parent, name)),
        }
    }

    fn inspect_update(&self, data: &mut Self::Data) {
        match (self.as_ref(), &mut data.inspect_data) {
            // None, always unset inspect data
            (None, ref mut inspect_data) => **inspect_data = None,

            // Missing inspect data, initialize it
            (Some(inner), None) => {
                data.inspect_data = Some(inner.inspect_create(&data.inspect_parent, &data.name));
            }

            // Update existing inspect data, for performance
            (Some(inner), Some(ref mut inner_inspect_data)) => {
                inner.inspect_update(inner_inspect_data);
            }
        }
    }
}

/// Renders inspect state. This trait should be implemented with
/// a relevant constraint on the base type.
pub trait Render {
    /// The base type, provided by the user.
    type Base;

    /// Inspect data, provided by implementors of this trait.
    type Data: Default;

    /// Initializes the inspect data from the current state of base.
    fn create(base: &Self::Base, parent: &Node, name: impl AsRef<str>) -> Self::Data;

    /// Updates the inspect data from the current state of base.
    fn update(base: &Self::Base, data: &mut Self::Data);
}

/// Generic smart pointer which owns an inspect subtree (either a Node or a
/// Property) for the duration of its lifetime. It dereferences to the
/// user-provided base type (similar to Arc and other smart pointers).
/// This type should rarely be used declared explictly. Rather, a specific smart
/// pointer (such as IValue) should be used.
pub struct IOwned<R: Render> {
    _base: R::Base,
    _inspect_data: R::Data,
}

impl<R: Render> IOwned<R> {
    /// Construct the smart pointer but don't populate any inspect state.
    pub fn new(value: R::Base) -> Self {
        let _inspect_data = R::Data::default();
        Self { _base: value, _inspect_data }
    }

    /// Construct the smart pointer and populate the inspect state under parent[name].
    pub fn attached(value: R::Base, parent: &Node, name: impl AsRef<str>) -> Self {
        let _inspect_data = R::create(&value, &parent, name);
        Self { _base: value, _inspect_data }
    }

    /// Returns a RAII guard which can be used for mutations. When the guard
    /// goes out of scope, the new inspect state is published.
    pub fn as_mut(&mut self) -> IOwnedMutGuard<'_, R> {
        IOwnedMutGuard(self)
    }

    /// Set the value, then update inspect state.
    pub fn iset(&mut self, value: R::Base) {
        self._base = value;
        R::update(&self._base, &mut self._inspect_data);
    }

    pub fn into_inner(self) -> R::Base {
        self._base
    }
}

impl<R: Render> Inspect for &mut IOwned<R> {
    fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        self._inspect_data = R::create(&self._base, &parent, name);
        Ok(())
    }
}

impl<R, B> fmt::Debug for IOwned<R>
where
    R: Render<Base = B>,
    B: fmt::Debug,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Debug::fmt(&self._base, f)
    }
}

impl<R, B> fmt::Display for IOwned<R>
where
    R: Render<Base = B>,
    B: fmt::Display,
{
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt::Display::fmt(&self._base, f)
    }
}

impl<R, B> Default for IOwned<R>
where
    R: Render<Base = B>,
    B: Default,
{
    fn default() -> Self {
        let _inspect_data = R::Data::default();
        let _base = B::default();
        Self { _base, _inspect_data }
    }
}

impl<R: Render> Deref for IOwned<R> {
    type Target = R::Base;
    fn deref(&self) -> &Self::Target {
        &self._base
    }
}

/// A RAII implementation of a scoped guard of an IOwned smart pointer. When
/// this structure is dropped (falls out of scope), the new inspect state will
/// be published.
pub struct IOwnedMutGuard<'a, R: Render>(&'a mut IOwned<R>);

impl<'a, R: Render> Deref for IOwnedMutGuard<'a, R> {
    type Target = R::Base;
    fn deref(&self) -> &R::Base {
        &self.0._base
    }
}

impl<'a, R: Render> DerefMut for IOwnedMutGuard<'a, R> {
    fn deref_mut(&mut self) -> &mut R::Base {
        &mut self.0._base
    }
}

impl<'a, R: Render> Drop for IOwnedMutGuard<'a, R> {
    fn drop(&mut self) {
        R::update(&self.0._base, &mut self.0._inspect_data);
    }
}

#[doc(hidden)]
pub struct ValueMarker<B: Unit>(PhantomData<B>);

impl<B: Unit> Render for ValueMarker<B> {
    type Base = B;
    type Data = B::Data;

    fn create(base: &Self::Base, parent: &Node, name: impl AsRef<str>) -> Self::Data {
        base.inspect_create(parent, name)
    }

    fn update(base: &Self::Base, data: &mut Self::Data) {
        base.inspect_update(data);
    }
}

/// An `Inspect` smart pointer for a type `B`, which renders an
/// inspect subtree as defined by `B: Unit`.
pub type IValue<B> = IOwned<ValueMarker<B>>;

impl<B: Unit> From<B> for IValue<B> {
    fn from(value: B) -> Self {
        Self::new(value)
    }
}

#[doc(hidden)]
pub struct DebugMarker<B: fmt::Debug>(PhantomData<B>);

impl<B: fmt::Debug> Render for DebugMarker<B> {
    type Base = B;
    type Data = StringProperty;

    fn create(base: &Self::Base, parent: &Node, name: impl AsRef<str>) -> Self::Data {
        parent.create_string(name, &format!("{:?}", base))
    }

    fn update(base: &Self::Base, data: &mut Self::Data) {
        data.set(&format!("{:?}", base));
    }
}

/// An `Inspect` smart pointer for a type `B`, which renders the debug
/// output of `B` as a string property.
pub type IDebug<B> = IOwned<DebugMarker<B>>;

impl<B: fmt::Debug> From<B> for IDebug<B> {
    fn from(value: B) -> Self {
        Self::new(value)
    }
}
