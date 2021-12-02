// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Contains testing utilities.

use std::sync::{Arc, Mutex};

/// A ref-counted counter that can be cloned and passed to a mock.
/// The mock can increment the counter on some event, and the test can verify that the event
/// occurred.
#[derive(Debug, Clone)]
pub struct CallCounter(Arc<Mutex<usize>>);

impl CallCounter {
    /// Creates a new counter and initializes its value.
    pub fn new(initial: usize) -> Self {
        Self(Arc::new(Mutex::new(initial)))
    }

    /// Read the count in the counter.
    pub fn count(&self) -> usize {
        *self.0.lock().unwrap()
    }

    /// Increment the value in the counter by one.
    pub fn increment(&self) {
        *self.0.lock().unwrap() += 1
    }
}
