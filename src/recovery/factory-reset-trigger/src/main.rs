// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use forced_fdr::perform_fdr_if_necessary;
use fuchsia_async as fasync;

#[fasync::run(1)]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["factory-reset-trigger"]).context("syslog init failed")?;
    perform_fdr_if_necessary().await;

    Ok(())
}
