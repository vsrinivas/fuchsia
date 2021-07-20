// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Helper for writing test cases.
//!
//! Create a TempTestEnv to provide some lite isolation in the way of a
//! temporary HOME, PATH env settings, etc.. These will be removed/restored when
//! the temp env is dropped.
//!
//! It's strongly advised to use with some form of test serialization such as
//! the serialize_test crate. The env is global to the process, so running tests
//! with env changes in parallel will be confusing.
//!
//! ```
//! #[cfg(test)]
//! mod tests {
//!     use {super::*, temp_test_env::TempTestEnv, serial_test::serial};
//!
//!     #[test]
//!     #[serial]
//!     fn test_something() {
//!         let test_env = TempTestEnv::new()?;
//!         [...]
//!     }
//! }
//! ```

mod temp_test_env;
pub use temp_test_env::*;
