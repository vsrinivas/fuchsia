// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements a slab-like data structure that uses a salted index

#![deny(missing_docs)]

use rand::Rng;
use std::collections::BinaryHeap;
use std::fmt;

#[derive(Clone)]
struct SaltWrapped<T> {
    salt: u32,
    value: Option<T>,
}

/// An index into a SaltSlab.
pub struct SaltedID<T> {
    obfuscated_id: u32,
    salt: u32,
    _type: std::marker::PhantomData<T>,
}

// Need to type this out instead of deriving it because derive(Copy) on SaltedID ==> T: Copy
impl<T> Clone for SaltedID<T> {
    fn clone(&self) -> SaltedID<T> {
        SaltedID {
            obfuscated_id: self.obfuscated_id,
            salt: self.salt,
            _type: std::marker::PhantomData,
        }
    }
}

impl<T> Copy for SaltedID<T> {}

impl<T> std::cmp::Ord for SaltedID<T> {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        self.tpl().cmp(&other.tpl())
    }
}

impl<T> std::cmp::PartialOrd for SaltedID<T> {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        self.tpl().partial_cmp(&other.tpl())
    }
}

impl<T> std::cmp::PartialEq for SaltedID<T> {
    fn eq(&self, other: &Self) -> bool {
        self.tpl().eq(&other.tpl())
    }
}

impl<T> std::cmp::Eq for SaltedID<T> {}

impl<T> std::hash::Hash for SaltedID<T> {
    fn hash<H: std::hash::Hasher>(&self, h: &mut H) {
        h.write_u32(self.obfuscated_id);
        h.write_u32(self.salt);
    }
}

/// Allows retrieving the T from a SaltedID<T>
pub trait ElemType {
    /// The element pointed to by an ID
    type Elem;
}

impl<T> ElemType for SaltedID<T> {
    type Elem = T;
}

impl<T> fmt::Debug for SaltedID<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}@{}", self.obfuscated_id, self.salt)
    }
}

impl<T> SaltedID<T> {
    /// Return an invalid ID.
    pub const fn invalid() -> Self {
        Self { obfuscated_id: 0, salt: 0, _type: std::marker::PhantomData }
    }

    /// Return if this ID is valid.
    /// Note: A valid ID is not guaranteed to refer to a live element,
    /// but an invalid ID is guaranteed not to.
    pub fn is_valid(&self) -> bool {
        self.salt != 0
    }

    fn id(&self, key: u32) -> usize {
        deobfuscate(self.obfuscated_id, key) as usize
    }

    fn tpl(&self) -> (u32, u32) {
        (self.obfuscated_id, self.salt)
    }

    fn new(id: u32, key: u32, salt: u32) -> Self {
        Self { obfuscated_id: obfuscate(id, key), salt, _type: std::marker::PhantomData }
    }
}

/// A slab-like data structure with indices that can be validated.
pub struct SaltSlab<T> {
    values: Vec<SaltWrapped<T>>,
    free: BinaryHeap<u32>,
    key: u32,
}

impl<T: std::fmt::Debug> std::fmt::Debug for SaltSlab<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "SaltSlab {{")?;
        for (id, value) in self.iter() {
            write!(f, "{:?}: {:?}, ", id, value)?;
        }
        write!(f, "}}")
    }
}

fn obfuscate(value: u32, key: u32) -> u32 {
    value.rotate_left(16) ^ key
}

fn deobfuscate(value: u32, key: u32) -> u32 {
    (value ^ key).rotate_right(16)
}

impl<T> Default for SaltSlab<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> SaltSlab<T> {
    /// Create a new (empty) slab.
    pub fn new() -> Self {
        Self { values: Vec::new(), free: BinaryHeap::new(), key: rand::thread_rng().gen::<u32>() }
    }

    /// Returns an iterator over all currently set values
    pub fn iter<'a>(&'a self) -> impl Iterator<Item = (SaltedID<T>, &'a T)> {
        let key = self.key;
        self.values.iter().enumerate().filter_map(move |(id, wrapped)| {
            let id = SaltedID::new(id as u32, key, wrapped.salt);
            wrapped.value.as_ref().map(move |value| (id, value))
        })
    }

    /// Returns a mutable iterator over all currently set values
    pub fn iter_mut<'a>(&'a mut self) -> impl Iterator<Item = (SaltedID<T>, &'a mut T)> {
        let key = self.key;
        self.values.iter_mut().enumerate().filter_map(move |(id, wrapped)| {
            let id = SaltedID::new(id as u32, key, wrapped.salt);
            wrapped.value.as_mut().map(move |value| (id, value))
        })
    }

    /// Insert `new` into the slab, return the ID for where it was inserted.
    pub fn insert(&mut self, new: T) -> SaltedID<T> {
        if let Some(index) = self.free.pop() {
            let value = &mut self.values[index as usize];
            assert!(value.value.is_none());
            value.salt = if value.salt == std::u32::MAX { 1 } else { value.salt + 1 };
            value.value = Some(new);
            SaltedID::new(index, self.key, value.salt)
        } else {
            let index = self.values.len();
            assert!(index < (std::u32::MAX as usize));
            self.values.push(SaltWrapped { salt: 1, value: Some(new) });
            SaltedID::new(index as u32, self.key, 1)
        }
    }

    /// Retrieve the element with id `id`, or `None` if this element is not present.
    pub fn get(&self, id: SaltedID<T>) -> Option<&T> {
        if id.id(self.key) >= self.values.len() {
            return None;
        }
        let value = &self.values[id.id(self.key)];
        if value.salt != id.salt {
            return None;
        }
        return value.value.as_ref();
    }

    /// Retrieve mutable element with id `id`, or `None` if this element is not present.
    pub fn get_mut(&mut self, id: SaltedID<T>) -> Option<&mut T> {
        if id.id(self.key) >= self.values.len() {
            return None;
        }
        let value = &mut self.values[id.id(self.key)];
        if value.salt != id.salt {
            return None;
        }
        return value.value.as_mut();
    }

    /// Remove element with id `id`. Return true if there was such an element, false otherwise.
    pub fn remove(&mut self, id: SaltedID<T>) -> bool {
        let index = id.id(self.key);
        if index >= self.values.len() {
            return false;
        }
        let value = &mut self.values[index];
        if value.salt != id.salt {
            return false;
        }
        if value.value.take().is_none() {
            return false;
        }
        self.free.push(index as u32);
        true
    }

    /// Return a shadow of this slab (id space is the same, data is different)
    pub fn shadow<U>(&self) -> ShadowSlab<T, U> {
        ShadowSlab { values: Vec::new(), key: self.key, _base_type: std::marker::PhantomData }
    }
}

/// A slab-like data structure that shadows some other `SaltSlab`
pub struct ShadowSlab<B, T> {
    values: Vec<SaltWrapped<T>>,
    key: u32,
    _base_type: std::marker::PhantomData<B>,
}

impl<B, T> ShadowSlab<B, T> {
    /// Init id `id` with value `new`
    pub fn init(&mut self, id: SaltedID<B>, new: T) -> &mut T {
        assert!(id.is_valid());
        while self.values.len() <= id.id(self.key) {
            self.values.push(SaltWrapped { salt: 0, value: None });
        }
        let v = &mut self.values[id.id(self.key)];
        v.salt = id.salt;
        v.value = Some(new);
        v.value.as_mut().unwrap()
    }

    /// Retrieve the element with id `id`, or `None` if this element is not present.
    pub fn get(&self, id: SaltedID<B>) -> Option<&T> {
        if id.id(self.key) >= self.values.len() {
            return None;
        }
        let value = &self.values[id.id(self.key)];
        if value.salt != id.salt {
            return None;
        }
        return value.value.as_ref();
    }

    /// Retrieve mutable element with id `id`, or `None` if this element is not present.
    pub fn get_mut(&mut self, id: SaltedID<B>) -> Option<&mut T> {
        if id.id(self.key) >= self.values.len() {
            return None;
        }
        let value = &mut self.values[id.id(self.key)];
        if value.salt != id.salt {
            return None;
        }
        return value.value.as_mut();
    }

    /// Remove element with id `id`. Return true if there was such an element, false otherwise.
    pub fn remove(&mut self, id: SaltedID<B>) -> bool {
        if id.id(self.key) >= self.values.len() {
            return false;
        }
        let value = &mut self.values[id.id(self.key)];
        if value.salt != id.salt {
            return false;
        }
        let r = value.value.is_some();
        value.value = None;
        r
    }
}

#[cfg(test)]
mod test {

    use super::*;

    #[test]
    fn invalid_works() {
        assert!(SaltSlab::<u8>::new().get(SaltedID::invalid()).is_none());
        assert!(SaltSlab::<u8>::new().get_mut(SaltedID::invalid()).is_none());
        assert_eq!(false, SaltSlab::<u8>::new().remove(SaltedID::invalid()));
        assert!(!SaltedID::<u8>::invalid().is_valid());
    }

    #[test]
    fn insert_get() {
        let mut slab = SaltSlab::new();
        let id = slab.insert(123u8);
        assert!(id.is_valid());
        assert_eq!(*slab.get(id).unwrap(), 123u8);
        assert!(slab.get(SaltedID::invalid()).is_none());
        *slab.get_mut(id).unwrap() = 124u8;
        assert_eq!(*slab.get(id).unwrap(), 124u8);
        assert!(id.is_valid());
        assert!(slab.remove(id));
        assert!(id.is_valid());
        assert!(slab.get(id).is_none());
        assert!(!slab.remove(id));
        assert!(id.is_valid());
    }

    #[test]
    fn reuse() {
        let mut slab = SaltSlab::new();
        let id1 = slab.insert(1u8);
        slab.remove(id1);
        let id2 = slab.insert(2u8);
        assert_eq!(id1.obfuscated_id, id2.obfuscated_id);
        assert_ne!(id1.salt, id2.salt);
        assert_eq!(*slab.get(id2).unwrap(), 2);
        assert!(slab.get(id1).is_none());
    }

    #[test]
    fn double_delete() {
        let mut slab = SaltSlab::new();
        let id1 = slab.insert(1u8);
        slab.remove(id1);
        slab.remove(id1);
        let id2 = slab.insert(2u8);
        assert_eq!(id1.obfuscated_id, id2.obfuscated_id);
        assert_ne!(id1.salt, id2.salt);
        assert_eq!(*slab.get(id2).unwrap(), 2);
        assert!(slab.get(id1).is_none());
        let id3 = slab.insert(3u8);
        assert_ne!(id1.obfuscated_id, id3.obfuscated_id);
        assert_eq!(*slab.get(id2).unwrap(), 2);
        assert_eq!(*slab.get(id3).unwrap(), 3);
    }
}
