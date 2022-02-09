// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Testing impls of [`crate::bridge::Bridge`].

use super::{Bridge, BridgeError, OptOutPreference};
use async_trait::async_trait;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

/// Bridge impl that always returns an error.
#[derive(Debug)]
pub struct Error;

#[async_trait(?Send)]
impl Bridge for Error {
    async fn get_opt_out(&self) -> Result<OptOutPreference, BridgeError> {
        Err(BridgeError::Busy)
    }

    async fn set_opt_out(&mut self, value: OptOutPreference) -> Result<(), BridgeError> {
        let _ = value;
        Err(BridgeError::Busy)
    }
}

/// A shared bool.
#[derive(Debug, Clone)]
pub struct Toggle(Arc<AtomicBool>);

impl Toggle {
    fn new(v: bool) -> Self {
        Self(Arc::new(AtomicBool::new(v)))
    }

    pub fn set(&self, v: bool) {
        self.0.store(v, Ordering::SeqCst)
    }

    fn get(&self) -> bool {
        self.0.load(Ordering::SeqCst)
    }
}

/// Bridge impl that uses non-persistent storage.
#[derive(Debug)]
pub struct Fake {
    value: OptOutPreference,
    error_toggle: Toggle,
}

impl Fake {
    /// Creates a new Fake that never fails requests.
    pub fn new(value: OptOutPreference) -> Self {
        Self::new_with_error_toggle(value).0
    }

    /// Creates a new Fake that will fail requests whenever the returned toggle is set to `true`.
    pub fn new_with_error_toggle(value: OptOutPreference) -> (Self, Toggle) {
        let toggle = Toggle::new(false);
        let s = Self { error_toggle: toggle.clone(), value };

        (s, toggle)
    }
}

#[async_trait(?Send)]
impl Bridge for Fake {
    async fn get_opt_out(&self) -> Result<OptOutPreference, BridgeError> {
        if self.error_toggle.get() {
            Err(BridgeError::Busy)
        } else {
            Ok(self.value)
        }
    }

    async fn set_opt_out(&mut self, value: OptOutPreference) -> Result<(), BridgeError> {
        if self.error_toggle.get() {
            Err(BridgeError::Busy)
        } else {
            self.value = value;
            Ok(())
        }
    }
}
