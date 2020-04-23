// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Spinel Driver

#![warn(rust_2018_idioms)]
#![warn(clippy::all)]

// NOTE: This line is a hack to work around some issues
//       with respect to external rust crates.
use spinel_pack::{self as spinel_pack};

mod flow_window;
mod spinel;

#[cfg(test)]
#[macro_export]
macro_rules! traceln (($($args:tt)*) => { eprintln!($($args)*); }; );

#[cfg(not(test))]
#[macro_export]
macro_rules! traceln (($($args:tt)*) => { }; );

#[macro_use]
mod prelude {
    pub use traceln;

    pub use anyhow::{format_err, Context as _};
    pub use fuchsia_async as fasync;
    pub use futures::prelude::*;
    pub use spinel_pack::prelude::*;
}

use crate::prelude::*;
use anyhow::Error;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["lowpan_spinel_driver"]).context("initialize logging")?;

    Ok(())
}
