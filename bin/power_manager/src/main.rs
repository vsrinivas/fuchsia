// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api)]
#![deny(warnings)]

extern crate fidl;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_vfs_watcher as vfs_watcher;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate parking_lot;

#[macro_use(make_ioctl)]
extern crate fdio;

#[macro_use(format_err)]
extern crate failure;

#[macro_use]
extern crate fuchsia_syslog as syslog;

mod power;

use app::server::ServicesServer;
use failure::{Error, ResultExt};
use fidl::endpoints2::{RequestStream, ServiceMarker};
use futures::prelude::*;
use parking_lot::Mutex;
use std::fs::File;
use std::io;
use std::path::PathBuf;
use std::sync::Arc;

extern crate fidl_fuchsia_power;
use fidl_fuchsia_power::{BatteryStatus, PowerManagerMarker, PowerManagerRequest,
                         PowerManagerRequestStream, PowerManagerWatcherProxy,
                         Status as power_status};

static POWER_DEVICE: &str = "/dev/class/power";

// Time to sleep between status update in seconds.
static SLEEP_TIME: u64 = 180;

struct BatteryStatusHelper {
    battery_status: BatteryStatus,
    watchers: Vec<PowerManagerWatcherProxy>,
}

#[inline]
fn get_current_time() -> i64 {
    let t = zx::Time::get(zx::ClockId::UTC);
    (t.nanos() / 1000) as i64
}

#[derive(Debug)]
enum WatchSuccess {
    Completed,
    BatteryAlreadyFound,
    AdapterAlreadyFound,
}

impl BatteryStatusHelper {
    pub fn new() -> BatteryStatusHelper {
        BatteryStatusHelper {
            battery_status: BatteryStatus {
                status: power_status::NotAvailable,
                battery_present: false,
                charging: false,
                discharging: false,
                critical: false,
                power_adapter_online: false,
                timestamp: get_current_time(),
                level: 0.0,
                remaining_battery_life: 0.0,
            },
            watchers: Vec::new(),
        }
    }

    // Adds and calls watcher
    fn add_watcher(&mut self, watcher: PowerManagerWatcherProxy) {
        match watcher.on_change_battery_status(&mut self.battery_status) {
            Ok(_) => self.watchers.push(watcher),
            Err(e) => fx_log_err!("did not add watcher: {:?}", e),
        }
    }

    fn run_watchers(&mut self) {
        let mut bs = &mut self.battery_status;
        self.watchers.retain(|w| {
            if let Err(e) = w.on_change_battery_status(&mut bs) {
                match e {
                    fidl::Error::ClientRead(zx::Status::PEER_CLOSED)
                    | fidl::Error::ClientWrite(zx::Status::PEER_CLOSED) => return false,
                    e => {
                        fx_log_err!("calling watcher: {:?}", e);
                        return true;
                    }
                }
            }
            return true;
        });
    }

    fn update_battery_status(
        &mut self, power_info: power::ioctl_power_get_info_t,
        battery_info: Option<power::ioctl_power_get_battery_info_t>,
    ) {
        let now = get_current_time();
        let old_battery_status = self.get_battery_status_copy();
        if let Some(bi) = battery_info {
            self.battery_status.battery_present = power_info.state & power::POWER_STATE_ONLINE != 0;
            self.battery_status.charging = power_info.state & power::POWER_STATE_CHARGING != 0;
            self.battery_status.discharging =
                power_info.state & power::POWER_STATE_DISCHARGING != 0;
            self.battery_status.critical = power_info.state & power::POWER_STATE_CRITICAL != 0;
            if self.battery_status.battery_present {
                self.battery_status.level =
                    (bi.remaining_capacity * 100) as f32 / bi.last_full_capacity as f32;
                if bi.present_rate < 0 {
                    self.battery_status.remaining_battery_life =
                        bi.remaining_capacity as f32 / (bi.present_rate * -1) as f32;
                } else {
                    self.battery_status.remaining_battery_life = -1.0;
                }
            }
        } else {
            self.battery_status.power_adapter_online = power_info.state == 0;
        }

        self.battery_status.status = power_status::Ok;
        fx_vlog!(1, "{:?}", self.battery_status);
        if old_battery_status != self.battery_status {
            self.battery_status.timestamp = now;
            self.run_watchers();
        }
    }

    // Updates the status
    fn update_status(&mut self, file: &File) -> Result<(), failure::Error> {
        let power_info = power::get_power_info(file).context("getting power_info")?;
        let mut battery_info: Option<power::ioctl_power_get_battery_info_t> = None;
        if power_info.power_type == power::POWER_TYPE_BATTERY {
            battery_info = Some(power::get_battery_info(file).context("getting battery_info")?);
        }
        self.update_battery_status(power_info, battery_info);
        Ok(())
    }

    fn get_battery_status_copy(&self) -> BatteryStatus {
        return BatteryStatus {
            ..self.battery_status
        };
    }
}

struct PowerManagerServer {
    battery_status_helper: Arc<Mutex<BatteryStatusHelper>>,
}

fn process_watch_event(
    filepath: &PathBuf, bsh: Arc<Mutex<BatteryStatusHelper>>, battery_device_found: &mut bool,
    adapter_device_found: &mut bool,
) -> Result<WatchSuccess, failure::Error> {
    let file = File::open(&filepath)?;
    let powerbuffer = power::get_power_info(&file).context("getting power_info")?;

    if powerbuffer.power_type == power::POWER_TYPE_BATTERY && *battery_device_found {
        return Ok(WatchSuccess::BatteryAlreadyFound);
    } else if powerbuffer.power_type == power::POWER_TYPE_AC && *adapter_device_found {
        return Ok(WatchSuccess::AdapterAlreadyFound);
    }
    let bsh2 = bsh.clone();
    power::add_listener(&file, move |file: &File| {
        let mut bsh2 = bsh2.lock();
        if let Err(e) = bsh2.update_status(&file) {
            fx_log_err!("{}", e);
        }
    }).context("adding listener")?;
    {
        let mut bsh = bsh.lock();
        bsh.update_status(&file).context("adding watch events")?;
    }

    if powerbuffer.power_type == power::POWER_TYPE_BATTERY {
        *battery_device_found = true;
        let bsh = bsh.clone();
        let timer = async::Interval::new(zx::Duration::from_seconds(SLEEP_TIME));
        let f = timer
            .map(move |_| {
                let mut bsh = bsh.lock();
                if let Err(e) = bsh.update_status(&file) {
                    fx_log_err!("{}", e);
                }
            }).collect::<()>();

        async::spawn(f);
    } else {
        *adapter_device_found = true;
    }
    Ok(WatchSuccess::Completed)
}

fn watch_power_device(
    bsh: Arc<Mutex<BatteryStatusHelper>>,
) -> impl Future<Output = Result<(), Error>> {
    future::ready(File::open(POWER_DEVICE))
        .map_err(|e| format_err!("cannot find power device: {:?}", e))
        .and_then(|file| {
            future::ready(vfs_watcher::Watcher::new(&file)
                .map_err(|e| format_err!("error watching power device: {:?}", e)))
        }).and_then(|watcher| {
            let mut adapter_device_found = false;
            let mut battery_device_found = false;
            watcher
                .map_ok(move |msg| {
                    if battery_device_found && adapter_device_found {
                        return;
                    }
                    let mut filepath = PathBuf::from(POWER_DEVICE);
                    filepath.push(msg.filename);
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
                            fx_log_info!(
                                "Skip '{:?}' as {} device already found",
                                filepath,
                                device_type
                            );
                        }
                        Err(err) => {
                            fx_log_err!(
                                "error for file while adding watch event '{:?}': {}",
                                filepath,
                                err
                            );
                        }
                    }
                })
                .try_collect::<()>()
                .err_into()
        })
}

fn spawn_power_manager(pm: PowerManagerServer, chan: async::Channel) {
    let state = Arc::new(pm);
    async::spawn(
        PowerManagerRequestStream::from_channel(chan)
            .map_ok(move |req| {
                let state = state.clone();
                match req {
                    PowerManagerRequest::GetBatteryStatus { responder, .. } => {
                        fx_log_info!("get_battery_status called");
                        let mut bsh = state.battery_status_helper.lock();
                        if let Err(e) = responder.send(&mut bsh.battery_status) {
                            fx_log_err!("sending battery status: {:?}", e);
                        }
                    }
                    PowerManagerRequest::Watch { watcher, .. } => {
                        fx_log_info!("watch called");
                        match watcher.into_proxy() {
                            Err(e) => {
                                fx_log_err!("getting watcher proxy: {:?}", e);
                            }
                            Ok(w) => {
                                let mut bsh = state.battery_status_helper.lock();
                                bsh.add_watcher(w);
                            }
                        }
                    }
                }
            })
            .try_collect::<()>()
            .unwrap_or_else(|e| fx_log_err!("{:?}", e))
    )
}

fn main() {
    syslog::init_with_tags(&["power_manager"]).expect("Can't init logger");
    fx_log_info!("start");
    if let Err(e) = main_pm() {
        fx_log_err!("{:?}", e);
    }
    fx_log_info!("stop");
}

fn main_pm() -> Result<(), Error> {
    let mut executor = async::Executor::new().context("unable to create executor")?;
    let bsh = Arc::new(Mutex::new(BatteryStatusHelper::new()));
    let bsh2 = bsh.clone();
    let f = watch_power_device(bsh2);

    async::spawn(f.unwrap_or_else(|e| {
        fx_log_err!("watch_power_device failed {:?}", e);
    }));

    let server_fut = ServicesServer::new()
        .add_service((PowerManagerMarker::NAME, move |chan| {
            let pm = PowerManagerServer {
                battery_status_helper: bsh.clone(),
            };
            spawn_power_manager(pm, chan);
        })).start()
        .map_err(|e| e.context("starting service server"))?;
    Ok(executor.run(server_fut, 2).context("running server")?) // 2 threads
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! cmp_fields {
        ($got:ident, $want:ident, [$($field:ident,)*], $test_no:expr) => { $(
            assert_eq!($got.$field, $want.$field, "test no: {}", $test_no);
        )* }
    }

    fn check_status(got: &BatteryStatus, want: &BatteryStatus, updated: bool, test_no: u32) {
        if updated {
            cmp_fields!(
                want,
                got,
                [
                    status,
                    battery_present,
                    charging,
                    critical,
                    power_adapter_online,
                    remaining_battery_life,
                ],
                test_no
            );
        } else {
            assert_eq!(want, got, "test: {}", test_no);
        }
    }

    fn get_default_battery_info() -> power::ioctl_power_get_battery_info_t {
        let mut battery_info = power::ioctl_power_get_battery_info_t::new();
        battery_info.last_full_capacity = 3000;
        battery_info.present_rate = -1000;
        battery_info.remaining_capacity = 2000;
        return battery_info;
    }

    #[test]
    fn update_battery_status() {
        let mut bsh = BatteryStatusHelper::new();
        let mut power_info = power::ioctl_power_get_info_t {
            power_type: power::POWER_TYPE_AC,
            state: 0,
        };
        let mut want = bsh.get_battery_status_copy();
        want.status = power_status::Ok;
        want.power_adapter_online = true;
        bsh.update_battery_status(power_info.clone(), None);
        check_status(&bsh.battery_status, &want, true, 1);

        let want = bsh.get_battery_status_copy();
        bsh.update_battery_status(power_info.clone(), None); // should not be updated
        check_status(&bsh.battery_status, &want, false, 2);

        let mut want = bsh.get_battery_status_copy();
        want.battery_present = true;
        want.level = 200.0 / 3.0;
        want.remaining_battery_life = 2.0;
        power_info.power_type = power::POWER_TYPE_BATTERY;
        power_info.state = 1;
        let battery_info = get_default_battery_info();
        bsh.update_battery_status(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_status, &want, true, 3);

        let mut want = bsh.get_battery_status_copy();
        power_info.state = 3;
        want.discharging = true;
        bsh.update_battery_status(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_status, &want, true, 4);

        let mut want = bsh.get_battery_status_copy();
        power_info.state = 5;
        want.discharging = false;
        want.charging = true;
        bsh.update_battery_status(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_status, &want, true, 5);

        let mut want = bsh.get_battery_status_copy();
        power_info.state = 13;
        want.discharging = false;
        want.charging = true;
        want.critical = true;
        bsh.update_battery_status(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_status, &want, true, 6);

        let mut want = bsh.get_battery_status_copy();
        power_info.state = 11;
        want.discharging = true;
        want.charging = false;
        want.critical = true;
        bsh.update_battery_status(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_status, &want, true, 7);
    }
}
