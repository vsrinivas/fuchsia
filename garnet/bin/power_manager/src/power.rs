// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate libc;

use fdio::{self, clone_channel};
use fidl_fuchsia_hardware_power::{BatteryInfo, SourceInfo, SourceSynchronousProxy};
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_vlog};
use fuchsia_zircon::{self as zx, Signals};
use futures::TryFutureExt;
use std::fs::File;
use std::io::{self, Result};
use std::marker::Send;

// Get the power info from file descriptor/hardware.power FIDL service
// Note that this file (/dev/class/power) is a left over artifact of the
// legacy power_manager implementation which was based on power IOCTLs.
// The file is still required as it provides the descriptor with which
// to bind the FIDL service, at least until ZX-3385 is complete, which
// will componentize drivers and allow them to provide discoverable FIDL
// services like everyone else.
pub fn get_power_info(file: &File) -> Result<SourceInfo> {
    let channel = clone_channel(&file)?;
    let mut power_source = SourceSynchronousProxy::new(channel);

    match power_source.get_power_info(zx::Time::INFINITE).map_err(|_| zx::Status::IO)? {
        result => {
            let (status, info) = result;
            fx_log_info!("got power_info {:#?} with status: {:#?}", info, status);
            Ok(info)
        }
    }
}

// Get the battery info from file descriptor/hardware.power FIDL service
// Note that this file (/dev/class/power) is a left over artifact of the
// legacy power_manager implementation which was based on power IOCTLs.
// The file is still required as it provides the descriptor with which
// to bind the FIDL service, at least until ZX-3385 is complete, which
// will componentize drivers and allow them to provide discoverable FIDL
// services like everyone else.
pub fn get_battery_info(file: &File) -> Result<BatteryInfo> {
    let channel = clone_channel(&file)?;
    //TODO(DNO-686) refactoring battery manager will make use of async proxies
    let mut power_source = SourceSynchronousProxy::new(channel);

    match power_source.get_battery_info(zx::Time::INFINITE).map_err(|_| zx::Status::IO)? {
        result => {
            let (status, info) = result;
            fx_log_info!("got battery_info {:#?} with status: {:#?}", info, status);
            Ok(info)
        }
    }
}

pub fn add_listener<F>(file: &File, callback: F) -> Result<()>
where
    F: 'static + Send + Fn(&File) + Sync,
{
    let channel = clone_channel(&file)?;
    //TODO(DNO-686) refactoring battery manager will make use of async proxies
    let mut power_source = SourceSynchronousProxy::new(channel);

    let (_status, handle) =
        power_source.get_state_change_event(zx::Time::INFINITE).map_err(|_| zx::Status::IO)?;

    let file_copy = file
        .try_clone()
        .map_err(|e| io::Error::new(e.kind(), format!("error copying power device file: {}", e)))?;

    fasync::spawn(
        async move {
            loop {
                fasync::OnSignals::new(&handle, Signals::USER_0).await?;
                fx_vlog!(1, "callback called {:?}", file_copy);
                callback(&file_copy);
            }
        }
            .unwrap_or_else(|e: failure::Error| {
                fx_log_err!("not able to apply listener to power device, wait failed: {:?}", e)
            }),
    );

    Ok(())
}
