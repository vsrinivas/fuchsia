// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A generic map with unique cookie values as long as entries are active.

// Implementation is currently a counter and a BTreeMap, but that could change.

use std::collections::BTreeMap;

#[derive(Debug)]
pub(crate) struct CookieMap<T> {
    counter: u64,
    inner: BTreeMap<u64, T>,
}

impl<T> CookieMap<T> {
    pub fn new() -> Self {
        CookieMap {
            counter: 0,
            inner: BTreeMap::new(),
        }
    }

    pub fn inner_map(&mut self) -> &mut BTreeMap<u64, T> {
        &mut self.inner
    }

    pub fn insert(&mut self, element: T) -> u64 {
        self.counter += 1;
        let key = self.counter;
        self.inner.insert(key, element);
        key
    }

    pub fn remove(&mut self, key: u64) -> Option<T> {
        self.inner.remove(&key)
    }
}
