// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async as fasync;

pub mod context;
pub mod energy_scan_command;
pub mod form_command;
pub mod invocation;
pub mod join_command;
pub mod leave_command;
pub mod list_command;
pub mod mfg_command;
pub mod network_scan_command;
pub mod provision_command;
pub mod repeat_command;
pub mod reset_command;
pub mod status_command;

use context::*;
use invocation::*;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: LowpanCtlInvocation = argh::from_env();
    let mut context: LowpanCtlContext = LowpanCtlContext::from_invocation(&args)?;

    args.exec(&mut context).await?;

    Ok(())
}
