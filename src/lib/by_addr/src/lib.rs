// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    hash::{Hash, Hasher},
    ptr,
    sync::Arc,
};

/// ByAddr allows two Arcs to be compared by address instead of using its contained
/// type's comparator.
#[derive(Debug)]
pub struct ByAddr<T: ?Sized>(pub Arc<T>);

impl<T: ?Sized> ByAddr<T> {
    /// Constructs a new ByAddr<T>.
    pub fn new(value: Arc<T>) -> Self {
        Self(value)
    }
}

impl<T: ?Sized> Clone for ByAddr<T> {
    fn clone(&self) -> Self {
        ByAddr::new(self.0.clone())
    }
}

impl<T: ?Sized> PartialEq<ByAddr<T>> for ByAddr<T> {
    fn eq(&self, other: &ByAddr<T>) -> bool {
        self.0.as_ref() as *const T == other.0.as_ref() as *const T
    }
}

impl<T: ?Sized> Hash for ByAddr<T> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        ptr::hash(self.0.as_ref(), state);
    }
}

impl<T: ?Sized> Eq for ByAddr<T> {}

#[cfg(test)]
mod tests {
    use {
        super::*,
        std::{collections::HashSet, sync::Arc},
    };

    #[test]
    fn by_addr_eq() {
        // Both arcs should contain same content so that we are sure that equality is based on
        // address and not string content.
        let common_str = "common_str".to_string();
        let str1 = Arc::new(common_str.clone());
        let str2 = Arc::new(common_str.clone());

        let by_addr = ByAddr::new(str1.clone());

        assert_eq!(by_addr, ByAddr::new(str1));
        assert_ne!(by_addr, ByAddr::new(str2));
    }

    #[test]
    fn by_addr_hash() {
        // Both arcs should contain same content so that we are sure that hashing is based on
        // address and not string content.
        let common_str = "common_str".to_string();
        let str1 = Arc::new(common_str.clone());
        let str2 = Arc::new(common_str.clone());

        let by_addr = ByAddr::new(str1.clone());

        let mut set: HashSet<ByAddr<String>> = HashSet::new();
        set.insert(by_addr);

        assert!(set.contains(&ByAddr::new(str1)));
        assert!(!set.contains(&ByAddr::new(str2)));
    }

    #[test]
    fn by_addr_clone() {
        let my_str = Arc::new("my str".to_owned());

        let by_addr = ByAddr::new(my_str.clone());
        let cloned = by_addr.clone();

        assert_eq!(by_addr, cloned);

        let mut set: HashSet<ByAddr<String>> = HashSet::new();
        set.insert(by_addr);

        assert!(set.contains(&cloned));
    }
}
