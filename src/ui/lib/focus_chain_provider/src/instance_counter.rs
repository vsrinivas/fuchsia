// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Arc,
};

/// A utility for counting instances of structs or closures, with tokens that increment the count
/// when instantiated and decrement it when `Drop`ped.
///
/// The advantage of this over simply using `Arc::strong_count` is that the owning handle (the
/// `InstanceCounter`) can also be cloned freely without contributing to the tally.
#[derive(Debug, Clone)]
pub(crate) struct InstanceCounter {
    inner: Arc<AtomicUsize>,
}

impl InstanceCounter {
    /// Creates a new counter with an initial count of 0.
    pub fn new() -> Self {
        Self { inner: Arc::new(0.into()) }
    }

    /// Creates a new token and increments the count by 1. When the token is dropped, the count will
    /// decrement by 1.
    pub fn make_token(&self) -> CountingToken {
        let token = CountingToken { inner: self.inner.clone() };
        self.inner.fetch_add(1, Ordering::SeqCst);
        token
    }

    /// Returns the number of outstanding tokens.
    pub fn count(&self) -> usize {
        self.inner.load(Ordering::SeqCst)
    }
}

/// See [`InstanceCounter::make_token`].
#[derive(Debug)]
pub(crate) struct CountingToken {
    inner: Arc<AtomicUsize>,
}

impl Drop for CountingToken {
    fn drop(&mut self) {
        self.inner.fetch_sub(1, Ordering::SeqCst);
    }
}

#[cfg(test)]
mod tests {
    use super::InstanceCounter;
    #[test]
    fn smoke_test() {
        let counter = InstanceCounter::new();
        assert_eq!(counter.count(), 0);

        let token_a = counter.make_token();
        assert_eq!(counter.count(), 1);

        let clone = counter.clone();
        assert_eq!(counter.count(), 1);
        assert_eq!(clone.count(), 1);

        let token_b = counter.make_token();
        assert_eq!(counter.count(), 2);
        assert_eq!(clone.count(), 2);

        drop(clone);
        assert_eq!(counter.count(), 2);

        drop(token_a);
        assert_eq!(counter.count(), 1);

        drop(token_b);
        assert_eq!(counter.count(), 0);
    }
}
