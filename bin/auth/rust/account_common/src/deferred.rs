// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// A simple wrapper to set an optional value after instantiation.
///
/// The value cannot be unset once set. When wrapped in a synchronization primitive, this type is
/// helpful as an input for asynchronous functions, letting them lazily create shared state on
/// first use.
pub struct DeferredOption<T> {
    value: Option<T>,
}

impl<T> DeferredOption<T> {
    /// Constructs an empty `DeferredOption`.
    pub fn new() -> DeferredOption<T> {
        DeferredOption { value: None }
    }

    /// Returns the current value.
    pub fn get(&self) -> Option<&T> {
        self.value.as_ref()
    }

    /// Sets or overrides the current value with `value`.
    pub fn set(&mut self, value: T) {
        self.value = Some(value);
    }

    /// Returns the current value if set, or sets and returns if using `setter` otherwise.
    pub fn get_or_create<'a, F>(&'a mut self, setter: F) -> &'a mut T
        where F: FnOnce() -> T
    {
        if self.value.is_none() {
            self.value = Some(setter());
        }
        self.value.as_mut().unwrap()
    }
}
