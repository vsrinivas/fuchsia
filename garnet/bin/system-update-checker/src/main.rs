// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

mod apply;
mod check;
mod errors;

use crate::apply::{apply_system_update, Initiator};
use crate::check::{check_for_system_update, SystemUpdateStatus};
use crate::errors::{Error, ErrorKind};
use failure::ResultExt;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_info;

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context(ErrorKind::CreateExecutor)?;
    fuchsia_syslog::init_with_tags(&["system-update-checker"]).expect("can't init logger");
    executor.run_singlethreaded(check_for_and_apply_system_update())
}

async fn check_for_and_apply_system_update() -> Result<(), Error> {
    match await!(check_for_system_update())? {
        SystemUpdateStatus::UpToDate { system_image } => {
            fx_log_info!("current system_image merkle: {}", system_image);
            fx_log_info!("system_image is already up-to-date");
        }
        SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image } => {
            fx_log_info!("current system_image merkle: {}", current_system_image);
            fx_log_info!("new system_image available: {}", latest_system_image);
            await!(apply_system_update(
                current_system_image,
                latest_system_image,
                Initiator::Manual
            ))?
        }
    }
    Ok(())
}
