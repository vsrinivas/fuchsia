// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Service for Fuchsia

use failure::{Error, ResultExt};
use fuchsia_async as fasync;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["lowpan"]).context("initialize logging")?;

    Ok(())
}
