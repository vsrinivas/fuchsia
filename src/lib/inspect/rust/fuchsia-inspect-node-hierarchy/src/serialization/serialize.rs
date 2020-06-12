// Copyright 2020impl  The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{ArrayContent, Bucket, NodeHierarchy, Property},
    base64,
    serde::ser::{Serialize, SerializeMap, SerializeSeq, Serializer},
};

impl<Key> Serialize for NodeHierarchy<Key>
where
    Key: AsRef<str>,
{
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let mut s = serializer.serialize_map(Some(1))?;
        let name = self.name.clone();
        s.serialize_entry(&name, &SerializableHierarchyFields { hierarchy: &self })?;
        s.end()
    }
}

pub struct SerializableHierarchyFields<'a, Key> {
    pub(in crate) hierarchy: &'a NodeHierarchy<Key>,
}

impl<'a, Key> Serialize for SerializableHierarchyFields<'a, Key>
where
    Key: AsRef<str>,
{
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let items = self.hierarchy.properties.len() + self.hierarchy.children.len();
        let mut s = serializer.serialize_map(Some(items))?;
        for property in self.hierarchy.properties.iter() {
            let name = property.name();
            let _ = match property {
                Property::String(_, value) => s.serialize_entry(name, &value)?,
                Property::Int(_, value) => s.serialize_entry(name, &value)?,
                Property::Uint(_, value) => s.serialize_entry(name, &value)?,
                Property::Double(_, value) => s.serialize_entry(name, &value)?,
                Property::Bool(_, value) => s.serialize_entry(name, &value)?,
                Property::Bytes(_, array) => {
                    s.serialize_entry(name, &format!("b64:{}", base64::encode(&array)))?
                }
                Property::DoubleArray(_, array) => {
                    s.serialize_entry(name, &array)?;
                }
                Property::IntArray(_, array) => {
                    s.serialize_entry(name, &array)?;
                }
                Property::UintArray(_, array) => {
                    s.serialize_entry(name, &array)?;
                }
            };
        }
        for child in self.hierarchy.children.iter() {
            s.serialize_entry(&child.name, &SerializableHierarchyFields { hierarchy: child })?;
        }
        s.end()
    }
}

macro_rules! impl_serialize_for_array_value {
    ($($type:ty,)*) => {
        $(
            impl Serialize for ArrayContent<$type> {
                fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
                    match self {
                        ArrayContent::Buckets(buckets) => {
                            let mut s = serializer.serialize_map(Some(1))?;
                            s.serialize_entry("buckets", &buckets)?;
                            s.end()
                        }
                        ArrayContent::Values(values) => {
                            let mut s = serializer.serialize_seq(Some(values.len()))?;
                            for value in values {
                                s.serialize_element(&value)?;
                            }
                            s.end()
                        }
                    }
                }
            }
        )*
    }
}

macro_rules! impl_serialize_for_array_bucket {
    ($($type:ty,)*) => {
        $(
            impl Serialize for Bucket<$type> {
                fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
                    let mut s = serializer.serialize_map(Some(3))?;
                    s.serialize_entry("count", &self.count)?;
                    s.serialize_entry("floor", &self.floor)?;
                    s.serialize_entry("upper_bound", &self.upper)?;
                    s.end()
                }
            }
        )*
    }
}

impl<'a> Serialize for Bucket<f64> {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        let mut s = serializer.serialize_map(Some(3))?;
        let parts = [("count", self.count), ("floor", self.floor), ("upper_bound", self.upper)];
        for (entry_key, value) in parts.iter() {
            if *value == std::f64::MAX || *value == std::f64::INFINITY {
                s.serialize_entry(entry_key, "Infinity")?;
            } else if *value == std::f64::MIN || *value == std::f64::NEG_INFINITY {
                s.serialize_entry(entry_key, "-Infinity")?;
            } else {
                s.serialize_entry(entry_key, value)?;
            }
        }
        s.end()
    }
}

impl_serialize_for_array_value![i64, u64, f64,];
impl_serialize_for_array_bucket![i64, u64,];
