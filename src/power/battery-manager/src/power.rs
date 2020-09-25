// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fdio::{self, clone_channel};
use fidl_fuchsia_hardware_power as hpower;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn, fx_vlog};
use fuchsia_vfs_watcher as vfs_watcher;
use fuchsia_zircon::{self as zx, Signals};
use futures::prelude::*;
use io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE};
use std::convert::From;
use std::fs::File;
use std::io::{self, Result as ioResult};
use std::marker::Send;
use std::path::PathBuf;
use std::sync::Arc;

use crate::battery_manager::BatteryManager;
use crate::LOG_VERBOSITY;

// TODO (fxbug.dev/33183): binding the FIDL service via file descriptor is still
// required for hardware FIDLs (implemented by ACPI battery driver).
// Once componentization of drivers is complete and they are capable of
// publishing their FIDL services, should be abl to remove POWER_DEVICE
// specifically and refactor the "power" module in general to leverage
// the discoverable service.
static POWER_DEVICE: &str = "/dev/class/power";

#[derive(Debug)]
enum WatchSuccess {
    Completed,
    BatteryAlreadyFound,
    AdapterAlreadyFound,
}

fn get_power_source_proxy(file: &File) -> ioResult<hpower::SourceProxy> {
    let channel = clone_channel(file)?;
    Ok(hpower::SourceProxy::new(fasync::Channel::from_channel(channel)?))
}

// Get the power info from file descriptor/hardware.power FIDL service
// Note that this file (/dev/class/power) is a left over artifact of the
// legacy power_manager implementation which was based on power IOCTLs.
// The file is still required as it provides the descriptor with which
// to bind the FIDL service, at least until fxbug.dev/33183 is complete, which
// will componentize drivers and allow them to provide discoverable FIDL
// services like everyone else.
pub async fn get_power_info(file: &File) -> ioResult<hpower::SourceInfo> {
    let power_source = get_power_source_proxy(&file)?;
    match power_source.get_power_info().map_err(|_| zx::Status::IO).await? {
        result => {
            let (status, info) = result;
            fx_vlog!(LOG_VERBOSITY, "::power:: get_power_info: {:#?}, status: {:#?}", info, status);
            Ok(info)
        }
    }
}

// Get the battery info from file descriptor/hardware.power FIDL service
// Note that this file (/dev/class/power) is a left over artifact of the
// legacy power_manager implementation which was based on power IOCTLs.
// The file is still required as it provides the descriptor with which
// to bind the FIDL service, at least until fxbug.dev/33183 is complete, which
// will componentize drivers and allow them to provide discoverable FIDL
// services like everyone else.
pub async fn get_battery_info(file: &File) -> ioResult<hpower::BatteryInfo> {
    let power_source = get_power_source_proxy(&file)?;
    match power_source.get_battery_info().map_err(|_| zx::Status::IO).await? {
        result => {
            let (status, info) = result;
            fx_vlog!(
                LOG_VERBOSITY,
                "::power:: get_battery_info: {:#?}, status: {:#?}",
                info,
                status
            );
            Ok(info)
        }
    }
}

fn add_listener<F>(file: &File, callback: F) -> ioResult<()>
where
    F: 'static + Send + Fn(hpower::SourceInfo, Option<hpower::BatteryInfo>) + Sync,
{
    let power_source = get_power_source_proxy(&file)?;
    let file_copy = file
        .try_clone()
        .map_err(|e| io::Error::new(e.kind(), format!("error copying power device file: {}", e)))?;

    fx_vlog!(LOG_VERBOSITY, "::power:: spawn device state change event listener");
    fasync::Task::spawn(
        async move {
            loop {
                // Note that get_state_change_event & wait on signal must
                // occur within the loop as it is the former call that
                // clears the signal bit following its setting during
                // the notification.
                let (_status, handle) =
                    power_source.get_state_change_event().map_err(|_| zx::Status::IO).await?;

                fx_vlog!(
                    LOG_VERBOSITY,
                    "::power event listener:: waiting on signal for state change event"
                );
                fasync::OnSignals::new(&handle, Signals::USER_0).await?;
                fx_vlog!(
                    LOG_VERBOSITY,
                    "::power event listener:: got signal for state change event"
                );

                let power_info = get_power_info(&file_copy).await?;
                let mut battery_info = None;
                if power_info.type_ == hpower::PowerType::Battery {
                    battery_info = Some(get_battery_info(&file_copy).await?);
                }
                callback(power_info, battery_info);
            }
        }
        .unwrap_or_else(|e: anyhow::Error| {
            fx_log_err!("not able to apply listener to power device, wait failed: {:?}", e)
        }),
    )
    .detach();

    Ok(())
}

async fn process_watch_event(
    filepath: &PathBuf,
    battery_manager: Arc<BatteryManager>,
    battery_device_found: &mut bool,
    adapter_device_found: &mut bool,
) -> Result<WatchSuccess, anyhow::Error> {
    fx_vlog!(LOG_VERBOSITY, "::power:: process_watch_event for {:#?}", &filepath);

    let file = File::open(&filepath)?;
    let power_info = get_power_info(&file).await?;

    let mut battery_info = None;
    if power_info.type_ == hpower::PowerType::Battery {
        if *battery_device_found {
            return Ok(WatchSuccess::BatteryAlreadyFound);
        } else {
            battery_info = Some(get_battery_info(&file).await?);
        }
    } else if power_info.type_ == hpower::PowerType::Ac && *adapter_device_found {
        return Ok(WatchSuccess::AdapterAlreadyFound);
    }

    // add the listener to wait on the signal/notification from
    // state event change interface provided by the hardware FIDL
    let battery_manager2 = battery_manager.clone();
    fx_vlog!(LOG_VERBOSITY, "::power:: process_watch_event add_listener with callback");
    add_listener(&file, move |p_info, b_info| {
        fx_vlog!(LOG_VERBOSITY, "::power event listener:: callback firing => UPDATE_STATUS");
        let battery_manager2 = battery_manager2.clone();
        fasync::Task::spawn(async move {
            if let Err(e) = battery_manager2.update_status(p_info.clone(), b_info.clone()) {
                fx_log_err!("{}", e);
            }
        })
        .detach()
    })?;

    if power_info.type_ == hpower::PowerType::Battery {
        *battery_device_found = true;
        // poll and update battery status to catch changes that might not
        // otherwise be notified (i.e. gradual charge/discharge)
        let battery_manager = battery_manager.clone();
        let mut timer = fasync::Interval::new(zx::Duration::from_seconds(60));
        fx_vlog!(LOG_VERBOSITY, "::power:: process_watch_event spawn periodic timer");

        fasync::Task::spawn(async move {
            while let Some(()) = (timer.next()).await {
                fx_vlog!(LOG_VERBOSITY, "::power:: periodic timer fired => UPDDATE_STATUS");
                let power_info = get_power_info(&file).await.unwrap();
                let battery_info = Some(get_battery_info(&file).await.unwrap());
                if let Err(e) =
                    battery_manager.update_status(power_info.clone(), battery_info.clone())
                {
                    fx_log_err!("{}", e);
                }
            }
        })
        .detach();
    } else {
        *adapter_device_found = true;
    }

    // update the status with the current state info from the watch event
    {
        fx_vlog!(LOG_VERBOSITY, "::power:: process_watch_event => UPDATE_STATUS");
        battery_manager
            .update_status(power_info.clone(), battery_info.clone())
            .context("adding watch events")?;
    }

    Ok(WatchSuccess::Completed)
}

pub async fn watch_power_device(battery_manager: Arc<BatteryManager>) -> Result<(), Error> {
    let dir_proxy = open_directory_in_namespace(POWER_DEVICE, OPEN_RIGHT_READABLE)?;
    let mut watcher = vfs_watcher::Watcher::new(dir_proxy).await?;
    let mut adapter_device_found = false;
    let mut battery_device_found = false;
    while let Some(msg) = (watcher.try_next()).await? {
        fx_vlog!(LOG_VERBOSITY, "::power:: watch_power_device trying next: {:#?}", &msg);
        if battery_device_found && adapter_device_found {
            continue;
        }
        let mut filepath = PathBuf::from(POWER_DEVICE);
        filepath.push(msg.filename);
        fx_log_info!("watch_power_device event for file: {:?}", &filepath);
        match process_watch_event(
            &filepath,
            battery_manager.clone(),
            &mut battery_device_found,
            &mut adapter_device_found,
        )
        .await
        {
            Ok(WatchSuccess::Completed) => {}
            Ok(early_return) => {
                let device_type = match early_return {
                    WatchSuccess::Completed => unreachable!(),
                    WatchSuccess::BatteryAlreadyFound => "battery",
                    WatchSuccess::AdapterAlreadyFound => "adapter",
                };
                fx_vlog!(
                    LOG_VERBOSITY,
                    "::power:: Skip '{:?}' as {} device already found",
                    filepath,
                    device_type
                );
            }
            Err(err) => {
                fx_log_warn!("Failed to add watch event for '{:?}': {}", filepath, err);
            }
        }
    }

    Ok(())
}
