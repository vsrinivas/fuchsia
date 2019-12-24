// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//use anyhow::Error;

use crate::common_utils::error::Sl4fError;

pub mod macros {
    pub use crate::parse_arg;
}

#[macro_export]
macro_rules! parse_arg {
    ($args:ident, $func:ident, $name:expr) => {
        match $args.get($name) {
            Some(v) => match v.$func() {
                Some(val) => Ok(val),
                None => Err(Sl4fError::new(format!("malformed {}", $name).as_str())),
            },
            None => Err(Sl4fError::new(format!("{} missing", $name).as_str())),
        }
    };
}
