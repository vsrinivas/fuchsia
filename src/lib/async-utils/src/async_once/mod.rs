// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Exposes the OnceCell crate for use in async code.

use async_lock::Mutex;
use once_cell::sync::OnceCell;
use std::future::Future;

/// Wrapper presenting an async interface to a OnceCell.
pub struct Once<T> {
    mutex: Mutex<()>,
    value: OnceCell<T>,
}

impl<T> Default for Once<T> {
    fn default() -> Self {
        Self { mutex: Mutex::new(()), value: OnceCell::new() }
    }
}

impl<T> Once<T> {
    /// Constructor.
    pub fn new() -> Self {
        Self { mutex: Mutex::new(()), value: OnceCell::new() }
    }

    /// Async wrapper around OnceCell's `get_or_init`.
    pub async fn get_or_init<'a, F>(&'a self, fut: F) -> &'a T
    where
        F: Future<Output = T>,
    {
        if let Some(t) = self.value.get() {
            t
        } else {
            let _mut = self.mutex.lock().await;
            // Someone raced us and just released the lock
            if let Some(t) = self.value.get() {
                t
            } else {
                let t = fut.await;
                self.value.set(t).unwrap_or_else(|_| panic!("race in async-cell!"));
                self.value.get().unwrap()
            }
        }
    }

    /// Async wrapper around OnceCell's `get_or_try_init`.
    pub async fn get_or_try_init<'a, F, E>(&'a self, fut: F) -> Result<&'a T, E>
    where
        F: Future<Output = Result<T, E>>,
    {
        if let Some(t) = self.value.get() {
            Ok(t)
        } else {
            let _mut = self.mutex.lock().await;
            // Someone raced us and just released the lock
            if let Some(t) = self.value.get() {
                Ok(t)
            } else {
                let r = fut.await;
                match r {
                    Ok(t) => {
                        self.value.set(t).unwrap_or_else(|_| panic!("race in async-cell!"));
                        Ok(self.value.get().unwrap())
                    }
                    Err(e) => Err(e),
                }
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use futures_lite::future::block_on;
    use std::sync::atomic::AtomicUsize;
    use std::sync::atomic::Ordering;

    #[test]
    fn test_get_or_init() {
        lazy_static::lazy_static!(
            static ref ONCE: Once<bool> = Once::new();
        );

        static COUNTER: AtomicUsize = AtomicUsize::new(0);

        let val = block_on(ONCE.get_or_init(async {
            COUNTER.fetch_add(1, Ordering::SeqCst);
            true
        }));

        assert_eq!(*val, true);
        assert_eq!(COUNTER.load(Ordering::SeqCst), 1);

        let val = block_on(ONCE.get_or_init(async {
            COUNTER.fetch_add(1, Ordering::SeqCst);
            false
        }));

        assert_eq!(*val, true);
        assert_eq!(COUNTER.load(Ordering::SeqCst), 1);
    }

    #[test]
    fn test_get_or_init_default_initializer() {
        lazy_static::lazy_static!(
            static ref ONCE: Once<bool> = Once::default();
        );

        static COUNTER: AtomicUsize = AtomicUsize::new(0);

        let val = block_on(ONCE.get_or_init(async {
            COUNTER.fetch_add(1, Ordering::SeqCst);
            true
        }));

        assert_eq!(*val, true);
        assert_eq!(COUNTER.load(Ordering::SeqCst), 1);

        let val = block_on(ONCE.get_or_init(async {
            COUNTER.fetch_add(1, Ordering::SeqCst);
            false
        }));

        assert_eq!(*val, true);
        assert_eq!(COUNTER.load(Ordering::SeqCst), 1);
    }

    #[test]
    fn test_get_or_try_init() {
        lazy_static::lazy_static!(
            static ref ONCE: Once<bool> = Once::new();
        );

        static COUNTER: AtomicUsize = AtomicUsize::new(0);

        let initializer = || async {
            let val = COUNTER.fetch_add(1, Ordering::SeqCst);
            if val == 0 {
                Err(std::io::Error::new(std::io::ErrorKind::Other, "first attempt fails"))
            } else {
                Ok(true)
            }
        };

        let val = block_on(ONCE.get_or_try_init(initializer()));

        assert!(val.is_err());
        assert_eq!(COUNTER.load(Ordering::SeqCst), 1);

        // The initializer gets another chance to run because the first attempt failed.
        let val = block_on(ONCE.get_or_try_init(initializer()));
        assert_eq!(*val.unwrap(), true);
        assert_eq!(COUNTER.load(Ordering::SeqCst), 2);

        // The initializer never runs again...
        let val = block_on(ONCE.get_or_try_init(initializer()));
        assert_eq!(*val.unwrap(), true);
        assert_eq!(COUNTER.load(Ordering::SeqCst), 2);
    }
}
