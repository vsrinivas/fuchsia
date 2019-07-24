// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implements a slab-like data structure that uses a salted index

use std::collections::BinaryHeap;
use std::fmt;

#[derive(Clone)]
struct SaltWrapped<T> {
    salt: u32,
    value: Option<T>,
}

/// An index into a SaltSlab.
#[derive(Clone, Copy, PartialEq, PartialOrd, Eq, Ord)]
pub struct SaltedID {
    id: u32,
    salt: u32,
}

impl fmt::Debug for SaltedID {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}@{}", self.id, self.salt)
    }
}

impl SaltedID {
    /// Return an invalid ID.
    pub const fn invalid() -> Self {
        Self {
            id: std::u32::MAX,
            salt: 0,
        }
    }

    /// Return if this ID is valid.
    /// Note: A valid ID is not guaranteed to refer to a live element, 
    /// but an invalid ID is guaranteed not to.
    pub fn is_valid(&self) -> bool {
        self.salt != 0
    }
}

/// A slab-like data structure with indices that can be validated.
pub struct SaltSlab<T> {
    values: Vec<SaltWrapped<T>>,
    free: BinaryHeap<u32>,
}

impl<T> Default for SaltSlab<T> {
    fn default() -> Self {
        Self::new()
    }
}

impl<T> SaltSlab<T> {
    /// Create a new (empty) slab.
    pub fn new() -> Self {
        Self {
            values: Vec::new(),
            free: BinaryHeap::new(),
        }
    }

    /// Insert `new` into the slab, return the ID for where it was inserted.
    pub fn insert(&mut self, new: T) -> SaltedID {
        if let Some(index) = self.free.pop() {
            let value = &mut self.values[index as usize];
            assert!(value.value.is_none());
            value.salt = if value.salt == std::u32::MAX {
                1
            } else {
                value.salt + 1
            };
            value.value = Some(new);
            SaltedID {
                id: index,
                salt: value.salt,
            }
        } else {
            let index = self.values.len();
            assert!(index < (std::u32::MAX as usize));
            self.values.push(SaltWrapped {
                salt: 1,
                value: Some(new),
            });
            SaltedID {
                id: index as u32,
                salt: 1,
            }
        }
    }

    /// Retrieve the element with id `id`, or `None` if this element is not present.
    pub fn get(&self, id: SaltedID) -> Option<&T> {
        if id.id as usize >= self.values.len() {
            return None;
        }
        let value = &self.values[id.id as usize];
        if value.salt != id.salt {
            return None;
        }
        return value.value.as_ref();
    }

    /// Retrieve mutable element with id `id`, or `None` if this element is not present.
    pub fn get_mut(&mut self, id: SaltedID) -> Option<&mut T> {
        if id.id as usize >= self.values.len() {
            return None;
        }
        let value = &mut self.values[id.id as usize];
        if value.salt != id.salt {
            return None;
        }
        return value.value.as_mut();
    }

    /// Remove element with id `id`. Return true if there was such an element, false otherwise.
    pub fn remove(&mut self, id: SaltedID) -> bool {
        if id.id as usize >= self.values.len() {
            return false;
        }
        let value = &mut self.values[id.id as usize];
        if value.salt != id.salt {
            return false;
        }
        let r = value.value.is_some();
        value.value = None;
        self.free.push(id.id);
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
        assert!(!SaltedID::invalid().is_valid());
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
        assert_eq!(id1.id, id2.id);
        assert_ne!(id1.salt, id2.salt);
        assert_eq!(*slab.get(id2).unwrap(), 2);
        assert!(slab.get(id1).is_none());
    }

}
