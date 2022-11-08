// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Rust equivalent of fbl::AutoCall
/// Automatically invokes a function when it goes
/// out of scope.
pub struct AutoCall<T>
where
    T: FnOnce(),
{
    val: Option<T>,
}

impl<T: FnOnce()> AutoCall<T> {
    pub fn new(val: T) -> AutoCall<T> {
        Self { val: Some(val) }
    }
}

impl<T: FnOnce()> Drop for AutoCall<T> {
    fn drop(&mut self) {
        if let Some(value) = self.val.take() {
            value();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[fuchsia::test]
    async fn drop_test() {
        let mut called = false;
        {
            let _call = AutoCall::new(|| {
                called = true;
            });
        }
        assert!(called);
    }
}
