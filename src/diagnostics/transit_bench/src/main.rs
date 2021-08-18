// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_async as fasync,
    fuchsia_criterion::{criterion::Criterion, FuchsiaCriterion},
};

mod ipc;
#[allow(dead_code)]
mod logs; // this module will DoS the test infrastructure as is
mod time;

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let mut crit = FuchsiaCriterion::new(Criterion::default());
    // logs::benches(&mut crit);
    time::benches(&mut crit);
    ipc::benches(&mut crit);
    Ok(())
}
