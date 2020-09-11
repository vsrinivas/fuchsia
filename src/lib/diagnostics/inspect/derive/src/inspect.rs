// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_inspect::{
    BoolProperty, BytesProperty, DoubleProperty, IntProperty, Node, Property, StringProperty,
    UintProperty,
};
use futures::lock;
use std::{cell, sync};
use thiserror::Error;

/// AttachError denotes a broken data acquisition invariant, such as when a
/// mutex is held during attachment.
#[derive(Error, Debug)]
#[error("could not attach to inspect: {:?}", .msg)]
pub struct AttachError {
    msg: String,
}

impl From<&str> for AttachError {
    fn from(msg: &str) -> Self {
        Self { msg: msg.to_string() }
    }
}

/// A trait for types that can be inspected using fuchsia_inspect and that
/// maintain their inspect state during their lifetime. Notably, this trait is
/// implemented for most fuchsia_inspect properties and the `IOwned` smart
/// pointers. Generic implementations are also provided for interior mutability
/// wrappers whose inner type also implement `Inspect`, such as RefCell and
/// various Mutex types. This method should generally be implemented for a
/// mutable reference (or a regular reference if the type has interior
/// mutability).
// TODO(fxbug.dev/50412): Add a derive-macro to auto generate this trait.
pub trait Inspect {
    /// Attaches `self` to the inspect tree, under `parent[name]`. Note that:
    ///
    /// - If this method is called on a type with interior mutability, it
    ///   will attempt to get exclusive access to the inner data, but will
    ///   not wait for a mutex to unlock. If it fails, an `AttachError` is
    ///   returned.
    /// - If this method is called on a fuchsia_inspect Property, it is
    ///   reinitialized to its default value.
    ///
    /// Therefore it is recommended to attach to inspect immediately after
    /// initialization, although not within constructors. Whether or not
    /// to use inspect should usually be a choice of the caller.
    ///
    /// NOTE: Implementors should avoid returning AttachError whenever
    /// possible. It is reserved for irrecoverable invariant violations
    /// (see above). Invalid data structure invariants are not attachment
    /// errors and should instead be ignored and optionally logged.
    fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError>;
}

/// Implement `Inspect` for a fuchsia_inspect property.
macro_rules! impl_inspect_property {
    ($prop_name:ident, $prop_name_cap:ident) => {
        paste::paste! {
            impl Inspect for &mut [<$prop_name_cap Property>] {
                fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError> {
                    let default = <[<$prop_name_cap Property>] as Property<'_>>::Type::default();
                    *self = parent.[<create_ $prop_name>](name, default);
                    Ok(())
                }
            }
        }
    };
}

impl_inspect_property!(uint, Uint);
impl_inspect_property!(int, Int);
impl_inspect_property!(double, Double);
impl_inspect_property!(bool, Bool);
impl_inspect_property!(string, String);
impl_inspect_property!(bytes, Bytes);

// Implement `Inspect` for interior mutability wrapper types.

impl<T> Inspect for &cell::RefCell<T>
where
    for<'a> &'a mut T: Inspect,
{
    fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        match self.try_borrow_mut() {
            Ok(mut inner) => inner.iattach(parent, name),
            Err(_) => Err("could not get exclusive access to cell::RefCell".into()),
        }
    }
}

impl<T> Inspect for &sync::Mutex<T>
where
    for<'a> &'a mut T: Inspect,
{
    fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        match self.try_lock() {
            Ok(mut inner) => inner.iattach(parent, name),
            Err(_) => Err("could not get exclusive access to std::sync::Mutex".into()),
        }
    }
}

impl<T> Inspect for &sync::RwLock<T>
where
    for<'a> &'a mut T: Inspect,
{
    fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        match self.try_write() {
            Ok(mut inner) => inner.iattach(parent, name),
            Err(_) => Err("could not get exclusive access to std::sync::RwLock".into()),
        }
    }
}

impl<T> Inspect for &parking_lot::Mutex<T>
where
    for<'a> &'a mut T: Inspect,
{
    fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        match self.try_lock() {
            Some(mut inner) => inner.iattach(parent, name),
            None => Err("could not get exclusive access to parking_lot::Mutex".into()),
        }
    }
}

impl<T> Inspect for &parking_lot::RwLock<T>
where
    for<'a> &'a mut T: Inspect,
{
    fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        match self.try_write() {
            Some(mut inner) => inner.iattach(parent, name),
            None => Err("could not get exclusive access to parking_lot::RwLock".into()),
        }
    }
}

impl<T> Inspect for &lock::Mutex<T>
where
    for<'a> &'a mut T: Inspect,
{
    fn iattach(self, parent: &Node, name: impl AsRef<str>) -> Result<(), AttachError> {
        match self.try_lock() {
            Some(mut inner) => inner.iattach(parent, name),
            None => Err("could not get exclusive access to futures::lock::Mutex".into()),
        }
    }
}

/// Extension trait for types that #[derive(Inspect)] (or implements
/// `Inspect for &mut T` some other way), providing a convenient way of
/// attaching to inspect during construction. See the `Inspect` trait for
/// more details.
pub trait WithInspect
where
    Self: Sized,
{
    /// Attaches self to the inspect tree. It is recommended to invoke this as
    /// part of construction. For example:
    ///
    /// `let yak =  Yak::new().with_inspect(inspector.root(), "my_yak")?;`
    fn with_inspect(self, parent: &Node, name: impl AsRef<str>) -> Result<Self, AttachError>;
}

impl<T> WithInspect for T
where
    for<'a> &'a mut T: Inspect,
{
    fn with_inspect(mut self, parent: &Node, name: impl AsRef<str>) -> Result<Self, AttachError> {
        self.iattach(parent, name).map(|()| self)
    }
}
