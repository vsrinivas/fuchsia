// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fuchsia_async as fasync, fuchsia_zircon as zx};

#[fasync::run_singlethreaded]
/// Simple program which effectively never returns. The program will exit after
/// running for 30 days.
async fn main() -> Result<(), Error> {
    fasync::Timer::new(fasync::Time::after(zx::Duration::from_hours(24 * 30))).await;
    Ok(())
}
