// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate tests the integration of the `test-harness-macro` and `test-harness` crates.
//! Specifically, it tests how the macros exported in `test-harness-macro` call into fns in
//! `test-harness`. This type of test is difficult to do from within `test-harness` itself, as the
//! `test-harness-macro`-defined macros call `test-harness` fns using the `::test-harness::.*` style
//! name resolution, and this name is not accessible from within the `test-harness` crate itself.

#![cfg(test)]
mod test {
    use anyhow::Error;
    use test_harness;
    #[test_harness::run_singlethreaded_test]
    async fn trivial_unit_harness(_unit_harness: ()) -> Result<(), Error> {
        Ok(())
    }
}
