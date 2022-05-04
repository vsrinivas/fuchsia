// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{hash::Hash, ops::Deref, rc::Rc};

use rustc_hash::FxHashSet;

#[derive(Debug)]
pub struct Interned<T: Eq + Hash> {
    data: Rc<T>,
}

impl<T: Eq + Hash> Deref for Interned<T> {
    type Target = T;

    fn deref(&self) -> &Self::Target {
        &self.data
    }
}

// Safe as long as `Interned` cannot clone the `Rc`.
unsafe impl<T: Eq + Hash + Sync> Sync for Interned<T> {}

#[derive(Debug, Default)]
pub struct Interner<T> {
    // We need to use `Rc` instead of `Weak` since `Weak` can have it's hash
    // value changed on the fly which is unsupported by `HashSet`.
    pointers: FxHashSet<Rc<T>>,
}

impl<T: Eq + Hash> Interner<T> {
    pub fn get(&mut self, val: T) -> Interned<T> {
        if let Some(rc) = self.pointers.get(&val) {
            return Interned { data: Rc::clone(rc) };
        }

        let data = Rc::new(val);

        self.pointers.insert(Rc::clone(&data));

        Interned { data }
    }

    pub fn compact(&mut self) {
        self.pointers.retain(|rc| Rc::strong_count(rc) > 1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn interned_same_ptr() {
        let mut interner = Interner::default();

        let foo0 = interner.get(String::from("foo"));
        let foo1 = interner.get(String::from("foo"));
        let bar = interner.get(String::from("bar"));

        assert!(Rc::ptr_eq(&foo0.data, &foo1.data));
        assert!(!Rc::ptr_eq(&foo0.data, &bar.data));
    }

    #[test]
    fn interned_drop_data() {
        let mut interner = Interner::default();

        let foo0 = interner.get(String::from("foo"));

        {
            let _foo1 = interner.get(String::from("foo"));
            let _bar = interner.get(String::from("bar"));

            assert_eq!(interner.pointers.len(), 2);
        }

        assert_eq!(interner.pointers.len(), 2);

        interner.compact();

        assert_eq!(interner.pointers.len(), 1);

        let foo1 = interner.get(String::from("foo"));
        assert!(Rc::ptr_eq(&foo0.data, &foo1.data));
    }
}
