// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async as fasync;

pub mod context;
pub mod invocation;
pub mod leave_command;
pub mod provision_command;
pub mod status_command;

use context::*;
use invocation::*;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut context: LowpanCtlContext = LowpanCtlContext::new();
    let args: LowpanCtlInvocation = argh::from_env();

    args.exec(&mut context).await
}
