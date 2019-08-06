// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fuchsia_inspect as inspect, fuchsia_inspect_contrib::nodes::ManagedNode, std::fmt};

const FALSE_VALUE: u64 = 0;
const TRUE_VALUE: u64 = 1;

/// Convert a type to the correct supported Inspect Property type. This is used in Bluetooth to
/// ensure consistent representations of values in Inspect.
///
/// Note: It represents them appropriately for Bluetooth but may not be the appropriate type
/// for other use cases. It shouldn't be used outside of the Bluetooth project.
pub trait ToProperty {
    type PropertyType;
    fn to_property(&self) -> Self::PropertyType;
}

impl ToProperty for bool {
    type PropertyType = u64;
    fn to_property(&self) -> Self::PropertyType {
        if *self {
            TRUE_VALUE
        } else {
            FALSE_VALUE
        }
    }
}

impl ToProperty for Option<bool> {
    type PropertyType = u64;
    fn to_property(&self) -> Self::PropertyType {
        self.as_ref().map(bool::to_property).unwrap_or(FALSE_VALUE)
    }
}

impl ToProperty for String {
    type PropertyType = String;
    fn to_property(&self) -> Self::PropertyType {
        self.to_string()
    }
}

impl<T, V> ToProperty for Vec<T>
where
    T: ToProperty<PropertyType = V>,
    V: ToString,
{
    type PropertyType = String;
    fn to_property(&self) -> Self::PropertyType {
        self.iter()
            .map(|t| <T as ToProperty>::to_property(t).to_string())
            .collect::<Vec<String>>()
            .join(", ")
    }
}

/// Vectors of T show up as a comma separated list string property. `None` types are
/// represented as an empty string.
impl<T, V> ToProperty for Option<Vec<T>>
where
    T: ToProperty<PropertyType = V>,
    V: ToString,
{
    type PropertyType = String;
    fn to_property(&self) -> Self::PropertyType {
        self.as_ref().map(ToProperty::to_property).unwrap_or_else(String::new)
    }
}

/// Convenience function to create a string containing the debug representation of an object that
/// implements `Debug`
pub trait DebugExt {
    fn debug(&self) -> String;
}

impl<T: fmt::Debug> DebugExt for T {
    fn debug(&self) -> String {
        format!("{:?}", self)
    }
}

/// Represents inspect data that is tied to a specific object. This inspect data and the object of
/// type T should always be bundled together.
pub trait InspectData<T> {
    fn new(object: &T, inspect: inspect::Node) -> Self;
}

pub trait IsInspectable
where
    Self: Sized + Send + Sync + 'static,
{
    type I: InspectData<Self>;
}

/// A wrapper around a type T that bundles some inspect data alongside instances of the type.
#[derive(Debug)]
pub struct Inspectable<T: IsInspectable> {
    pub(crate) inner: T,
    pub(crate) inspect: T::I,
}

impl<T: IsInspectable> Inspectable<T> {
    /// Create a new instance of an `Inspectable` wrapper type containing the T instance that
    /// it wraps along with populated inspect data.
    pub fn new(object: T, inspect: inspect::Node) -> Inspectable<T> {
        Inspectable { inspect: T::I::new(&object, inspect), inner: object }
    }
}

/// `Inspectable`s can always safely be immutably dereferenced as the type T that they wrap
/// because the data will not be mutated through this reference.
impl<T: IsInspectable> std::ops::Deref for Inspectable<T> {
    type Target = T;
    fn deref(&self) -> &Self::Target {
        &self.inner
    }
}

/// A trait representing the inspect data for a type T that will never be mutated. This trait
/// allows for a simpler "fire and forget" representation of the inspect data associated with an
/// object. This is because inspect handles for the data will never need to be accessed after
/// creation.
pub trait ImmutableDataInspect<T> {
    fn new(data: &T, manager: ManagedNode) -> Self;
}

/// "Fire and forget" representation of some inspect data that does not allow access inspect
/// handles after they are created.
pub struct ImmutableDataInspectManager {
    pub(crate) _manager: ManagedNode,
}

impl<T, I: ImmutableDataInspect<T>> InspectData<T> for I {
    /// Create a new instance of some type `I` that represents the immutable inspect data for a type
    /// `T`. This is done by handing `I` a `ManagedNode` instead of a `Node` and calling into the
    /// monomorphized version of ImmutableDataInspect<T> for I.
    fn new(data: &T, inspect: inspect::Node) -> I {
        I::new(data, ManagedNode::new(inspect))
    }
}

/// A placeholder node that can be used in tests that do not care about the `Node` value
pub fn placeholder_node() -> fuchsia_inspect::Node {
    fuchsia_inspect::Inspector::new().root().create_child("placeholder")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn bool_to_property() {
        let b = false.to_property();
        assert_eq!(b, FALSE_VALUE);
        let b = true.to_property();
        assert_eq!(b, TRUE_VALUE);
    }

    #[test]
    fn optional_bool_to_property() {
        let b: u64 = None::<bool>.to_property();
        assert_eq!(b, FALSE_VALUE);
        let b = Some(false).to_property();
        assert_eq!(b, FALSE_VALUE);
        let b = Some(true).to_property();
        assert_eq!(b, TRUE_VALUE);
    }

    #[test]
    fn string_vec_to_property() {
        let s = Vec::<String>::new().to_property();
        assert_eq!(s, "");
        let s = vec!["foo".to_string()].to_property();
        assert_eq!(s, "foo");
        let s = vec!["foo".to_string(), "bar".to_string(), "baz".to_string()].to_property();
        assert_eq!(s, "foo, bar, baz");
    }

    #[test]
    fn optional_string_vec_to_property() {
        let s = Some(vec!["foo".to_string(), "bar".to_string(), "baz".to_string()]).to_property();
        assert_eq!(s, "foo, bar, baz");
    }

    #[test]
    fn debug_string() {
        #[derive(Debug)]
        struct Foo {
            bar: u8,
            baz: &'static str,
        }
        let foo = Foo { bar: 1, baz: "baz value" };
        assert_eq!(format!("{:?}", foo), foo.debug());
    }
}
