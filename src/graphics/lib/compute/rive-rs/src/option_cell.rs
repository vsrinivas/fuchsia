// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::Cell, fmt};

pub struct OptionCell<T>(Cell<Option<T>>);

impl<T> OptionCell<T> {
    pub fn new() -> Self {
        Self(Cell::new(None))
    }
}

impl<T: Clone> OptionCell<T> {
    pub fn get(&self) -> Option<T> {
        let val = self.0.take();
        self.0.set(val.clone());
        val
    }
}

impl<T> OptionCell<T> {
    pub fn maybe_init(&self, f: impl FnOnce() -> T) {
        let val = self.0.take();
        self.0.set(Some(val.unwrap_or_else(f)));
    }

    pub fn set(&self, val: Option<T>) {
        self.0.set(val);
    }

    pub fn with<U>(&self, mut f: impl FnMut(Option<&T>) -> U) -> U {
        let val = self.0.take();
        let result = f(val.as_ref());
        self.0.set(val);

        result
    }

    pub fn with_mut<U>(&self, mut f: impl FnMut(Option<&mut T>) -> U) -> U {
        let mut val = self.0.take();
        let result = f(val.as_mut());
        self.0.set(val);

        result
    }
}

impl<T> Default for OptionCell<T> {
    fn default() -> Self {
        Self(Cell::new(None))
    }
}

impl<T: fmt::Debug> fmt::Debug for OptionCell<T> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        self.with(|val| fmt::Debug::fmt(&val, f))
    }
}
