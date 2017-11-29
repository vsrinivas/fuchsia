// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate fidl;
extern crate fuchsia_app;
extern crate fuchsia_zircon as zircon;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate mxruntime;
extern crate mxruntime_sys;
extern crate tokio_core;
extern crate tokio_fuchsia;

#[macro_use(make_ioctl)]
extern crate fdio;

pub mod power;

use fidl::{InterfacePtr, ClientEnd};
use fuchsia_app::server::bootstrap_server;
use futures::future;
use std::fmt;
use std::fs::File;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::thread;
use tokio_core::reactor;

extern crate garnet_public_lib_power_fidl;
use garnet_public_lib_power_fidl::{BatteryStatus, PowerManager, PowerManagerWatcher,
                                   Status as power_status};

static POWER_DEVICE: &'static str = "/dev/class/power";

//Time to sleep between status update in seconds.
static SLEEP_TIME: u64 = 180;

struct BatteryStatusHelper {
    battery_status: BatteryStatus,
    watchers: Vec<PowerManagerWatcher::Proxy>,
}

#[inline]
fn get_current_time() -> i64 {
    let t = zx::Time::get(zx::ClockId::UTC);
    return (t.nanos() / 1000) as i64;
}

#[derive(Debug)]
enum WatchSuccess {
    Completed,
    BatteryAlreadyFound,
    AdapterAlreadyFound,
}

enum PowerHelperErr {
    GetPowerInfo(io::Error),
    GetBatteryInfo(io::Error),
    AddPowerListener(io::Error),
}

impl fmt::Display for PowerHelperErr {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            PowerHelperErr::GetPowerInfo(ref e) => write!(f, "error getting power_info: {}", e),
            PowerHelperErr::GetBatteryInfo(ref e) => write!(f, "error getting battery_info: {}", e),
            PowerHelperErr::AddPowerListener(ref e) => {
                write!(f, "error adding power_listener: {}", e)
            }
        }
    }
}

struct UpdateStatusErr(PowerHelperErr);

impl From<PowerHelperErr> for UpdateStatusErr {
    fn from(error: PowerHelperErr) -> UpdateStatusErr {
        UpdateStatusErr(error)
    }
}

impl fmt::Display for UpdateStatusErr {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "error while updating battery status: {}", self.0)
    }
}

enum WatchError {
    FileIo(io::Error),
    GetPowerInfo(PowerHelperErr),
    UpdateStatus(UpdateStatusErr),
    AddPowerListener(PowerHelperErr),
}

impl fmt::Display for WatchError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            WatchError::FileIo(ref e) => write!(f, "error with watch events: {}", e),
            WatchError::GetPowerInfo(ref e) |
            WatchError::AddPowerListener(ref e) => write!(f, "error with watch events: {}", e),
            WatchError::UpdateStatus(ref e) => write!(f, "error with watch events: {}", e),
        }
    }
}

impl From<UpdateStatusErr> for WatchError {
    fn from(error: UpdateStatusErr) -> WatchError {
        WatchError::UpdateStatus(error)
    }
}

impl BatteryStatusHelper {
    pub fn new() -> BatteryStatusHelper {
        BatteryStatusHelper {
            battery_status: BatteryStatus {
                status: power_status::NotAvailable,
                batteryPresent: false,
                charging: false,
                discharging: false,
                critical: false,
                powerAdapterOnline: false,
                timestamp: get_current_time(),
                level: 0.0,
                remainingBatteryLife: 0.0,
            },
            watchers: Vec::new(),
        }
    }

    // Adds and calls watcher
    fn add_watcher(&mut self, watcher: PowerManagerWatcher::Proxy) {
        let _ = watcher.on_change_battery_status(self.get_battery_status_copy());
        self.watchers.push(watcher);
    }

    fn run_watchers(&mut self) {
        let battery_status = self.get_battery_status_copy();
        self.watchers.retain(|w| {
            if let Err(e) = w.on_change_battery_status(BatteryStatus { ..battery_status }) {
                match e {
                    fidl::Error::IoError(ref ie)
                        if ie.kind() == io::ErrorKind::ConnectionAborted => {
                        return false;
                    }
                    e => {
                        eprintln!("power_manger: Error call watcher: {:?}", e);
                    }
                }
            }
            return true;
        });
    }

    // Updates the status
    fn update_status(&mut self, file: &File) -> Result<(), UpdateStatusErr> {
        let power_info = power::get_power_info(file).map_err(
            PowerHelperErr::GetPowerInfo,
        )?;
        let mut battery_info: Option<power::ioctl_power_get_battery_info_t> = None;
        if power_info.power_type == power::POWER_TYPE_BATTERY {
            battery_info = Some(power::get_battery_info(file).map_err(
                PowerHelperErr::GetBatteryInfo,
            )?);
        }
        let now = get_current_time();
        let old_battery_status = self.get_battery_status_copy();
        if let Some(bi) = battery_info {
            self.battery_status.batteryPresent = power_info.state & power::POWER_STATE_ONLINE != 0;
            self.battery_status.charging = power_info.state & power::POWER_STATE_CHARGING != 0;
            self.battery_status.discharging = power_info.state & power::POWER_STATE_DISCHARGING !=
                0;
            self.battery_status.critical = power_info.state & power::POWER_STATE_CRITICAL != 0;
            if self.battery_status.batteryPresent {
                self.battery_status.level = (bi.remaining_capacity * 100) as f32 /
                    bi.last_full_capacity as f32;
                if bi.present_rate < 0 {
                    self.battery_status.remainingBatteryLife = bi.remaining_capacity as f32 /
                        (bi.present_rate * -1) as f32;
                } else {
                    self.battery_status.remainingBatteryLife = -1.0;
                }
            }
        } else {
            self.battery_status.powerAdapterOnline = power_info.state &
                power::POWER_STATE_ONLINE != 0;
        }

        self.battery_status.status = power_status::Ok;
        if old_battery_status != self.battery_status {
            self.battery_status.timestamp = now;
            self.run_watchers();
        }
        Ok(())
    }

    fn get_battery_status_copy(&self) -> BatteryStatus {
        return BatteryStatus { ..self.battery_status };
    }
}

struct PowerManagerServer {
    battery_status_helper: Arc<Mutex<BatteryStatusHelper>>,
    handle: reactor::Handle,
}

impl PowerManager::Server for PowerManagerServer {
    type GetBatteryStatus = fidl::ServerImmediate<BatteryStatus>;

    fn get_battery_status(&mut self) -> Self::GetBatteryStatus {
        let bsh = self.battery_status_helper.lock().unwrap();
        return future::ok(bsh.get_battery_status_copy());
    }

    type Watch = fidl::ServerImmediate<()>;
    fn watch(
        &mut self,
        watcher: InterfacePtr<ClientEnd<PowerManagerWatcher::Service>>,
    ) -> Self::Watch {
        match PowerManagerWatcher::new_proxy(watcher.inner, &self.handle) {
            Err(e) => {
                eprintln!("power_manger: error getting watcher proxy: {:?}", e);
            }
            Ok(w) => {
                let mut bsh = self.battery_status_helper.lock().unwrap();
                bsh.add_watcher(w);
            }
        }
        return future::ok(());
    }
}



fn process_watch_event(
    filepath: &PathBuf,
    bsh: Arc<Mutex<BatteryStatusHelper>>,
    battery_device_found: &mut bool,
    adapter_device_found: &mut bool,
) -> Result<WatchSuccess, WatchError> {
    let file = File::open(&filepath).map_err(WatchError::FileIo)?;
    let powerbuffer = power::get_power_info(&file)
        .map_err(PowerHelperErr::GetPowerInfo)
        .map_err(WatchError::GetPowerInfo)?;
    if powerbuffer.power_type == power::POWER_TYPE_BATTERY && *battery_device_found {
        return Ok(WatchSuccess::BatteryAlreadyFound);
    } else if powerbuffer.power_type == power::POWER_TYPE_AC && *adapter_device_found {
        return Ok(WatchSuccess::AdapterAlreadyFound);
    }
    let bsh2 = bsh.clone();
    power::add_listener(&file, move |file: &File| {
        let mut bsh2 = bsh2.lock().unwrap();
        if let Err(e) = bsh2.update_status(&file) {
            eprintln!("power_manager: {}", e);
        }
    }).map_err(PowerHelperErr::AddPowerListener)
        .map_err(WatchError::AddPowerListener)?;
    {
        let mut bsh = bsh.lock().unwrap();
        bsh.update_status(&file).map_err(WatchError::UpdateStatus)?;
    }

    if powerbuffer.power_type == power::POWER_TYPE_BATTERY {
        let bsh = bsh.clone();
        thread::spawn(move || loop {
            std::thread::sleep(std::time::Duration::from_secs(SLEEP_TIME));
            let mut bsh = bsh.lock().unwrap();
            if let Err(e) = bsh.update_status(&file) {
                eprintln!("power_manger: {}", e);
            }
        });
        *battery_device_found = true;
    } else {
        *adapter_device_found = true;
    }
    Ok(WatchSuccess::Completed)
}

fn watch_power_device(bsh: Arc<Mutex<BatteryStatusHelper>>) -> Result<(), String> {
    let file = File::open(POWER_DEVICE).map_err(|e| {
        format!("cannot find power device: {:?}", e)
    })?;
    let mut adapter_device_found = false;
    let mut battery_device_found = false;
    let c = |_: fdio::WatchEvent, p: &Path| -> Result<(), zircon::Status> {
        if let Some(path) = p.to_str() {
            if path.is_empty() {
                return Ok(());
            }
            let mut filepath = PathBuf::from(POWER_DEVICE);
            filepath.push(path);
            match process_watch_event(
                &filepath,
                bsh.clone(),
                &mut battery_device_found,
                &mut adapter_device_found,
            ) {
                Ok(WatchSuccess::Completed) => {}
                Ok(early_return) => {
                    let device_type = match early_return {
                        WatchSuccess::Completed => unreachable!(),
                        WatchSuccess::BatteryAlreadyFound => "battery",
                        WatchSuccess::AdapterAlreadyFound => "adapter",
                    };
                    println!(
                        "power_manager: Skip '{:?}' as {} device already found",
                        filepath,
                        device_type
                    );
                }
                Err(err) => {
                    eprintln!("power_manger: error for file '{:?}': {}", filepath, err);
                }
            }
            if battery_device_found && adapter_device_found {
                return Err(zircon::Status::STOP);
            }
        }
        Ok(())
    };
    let status = fdio::watch_directory(&file, zircon::sys::ZX_TIME_INFINITE, c);
    if status != zircon::Status::STOP && status != zircon::Status::OK {
        return Err(format!("Error waching power device: {:?}", status));
    }
    Ok(())
}


fn main() {
    if let Err(e) = main_pm() {
        eprintln!("power_manger: Error: {:?}", e);
    }
}

fn main_pm() -> Result<(), String> {
    let mut core = reactor::Core::new().map_err(|e| {
        format!("unable to create core: {:?}", e)
    })?;
    let bsh = Arc::new(Mutex::new(BatteryStatusHelper::new()));
    let bsh2 = bsh.clone();
    let handle = core.handle();
    let watch_thread = thread::spawn(move || if let Err(e) = watch_power_device(bsh2) {
        eprintln!("power_manger: {:?}", e);
    });
    let server = bootstrap_server(handle.clone(), move || {
        let pm = PowerManagerServer {
            battery_status_helper: bsh.clone(),
            handle: handle.clone(),
        };
        PowerManager::Dispatcher(pm)
    }).map_err(|e| format!("running server: {:?}", e))?;
    core.run(server).map_err({
        |e| format!("running server: {:?}", e)
    })?;
    watch_thread.join().map_err(|e| {
        format!("err waiting for thread: {:?}", e)
    })
}
