// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]
#![deny(warnings)]

use failure::{Error, ResultExt};
use fidl_fuchsia_hardware_power as hpower;
use fidl_fuchsia_power as fpower;
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{self as syslog, fx_log_err, fx_log_info};
use fuchsia_vfs_watcher as vfs_watcher;
use fuchsia_zircon as zx;
use futures::prelude::*;
use parking_lot::Mutex;
use std::convert::{From, Into};
use std::fs::File;
use std::path::PathBuf;
use std::sync::Arc;

mod power;

// TODO (ZX-3385): still required for FIDL service binding (via fd)
// once componentization of drivers is complete and they are
// capable of publishing their FIDL services, should be able
// to remove this.
static POWER_DEVICE: &str = "/dev/class/power";

// Time to sleep between status update in seconds.
static SLEEP_TIME: i64 = 180;

#[derive(Debug, PartialEq)]
struct TimeRemainingWrapper(fpower::TimeRemaining);

impl Clone for TimeRemainingWrapper {
    fn clone(&self) -> TimeRemainingWrapper {
        match self {
            TimeRemainingWrapper(fpower::TimeRemaining::FullCharge(i)) => {
                TimeRemainingWrapper(fpower::TimeRemaining::FullCharge(i.clone()))
            }
            TimeRemainingWrapper(fpower::TimeRemaining::BatteryLife(i)) => {
                TimeRemainingWrapper(fpower::TimeRemaining::BatteryLife(i.clone()))
            }
            _ => TimeRemainingWrapper(fpower::TimeRemaining::Indeterminate(0)),
        }
    }
}

impl From<TimeRemainingWrapper> for fpower::TimeRemaining {
    fn from(time_remaining: TimeRemainingWrapper) -> Self {
        match time_remaining {
            TimeRemainingWrapper(fpower::TimeRemaining::FullCharge(i)) => {
                fpower::TimeRemaining::FullCharge(i)
            }
            TimeRemainingWrapper(fpower::TimeRemaining::BatteryLife(i)) => {
                fpower::TimeRemaining::BatteryLife(i)
            }
            _ => fpower::TimeRemaining::Indeterminate(0),
        }
    }
}

#[derive(Debug, PartialEq, Clone)]
struct BatteryInfoWrapper {
    status: fpower::BatteryStatus,
    charge_status: fpower::ChargeStatus,
    charge_source: fpower::ChargeSource,
    level_percent: f32,
    level_status: fpower::LevelStatus,
    health: fpower::HealthStatus,
    time_remaining: TimeRemainingWrapper,
    timestamp: i64,
}

// Default initization for BatteryInfo state prior to obtaining
// actual values from hardware FIDL protocol implementation (driver)
impl Default for BatteryInfoWrapper {
    fn default() -> BatteryInfoWrapper {
        BatteryInfoWrapper {
            status: fpower::BatteryStatus::NotAvailable,
            charge_status: fpower::ChargeStatus::Unknown,
            charge_source: fpower::ChargeSource::Unknown,
            level_percent: 0.0,
            level_status: fpower::LevelStatus::Ok,
            health: fpower::HealthStatus::Unknown,
            time_remaining: TimeRemainingWrapper(fpower::TimeRemaining::Indeterminate(0)),
            timestamp: get_current_time(),
        }
    }
}

impl From<BatteryInfoWrapper> for fpower::BatteryInfo {
    fn from(battery_info: BatteryInfoWrapper) -> Self {
        let res = fpower::BatteryInfo {
            status: Some(battery_info.status),
            charge_status: Some(battery_info.charge_status),
            charge_source: Some(battery_info.charge_source),
            level_percent: Some(battery_info.level_percent),
            level_status: Some(battery_info.level_status),
            health: Some(battery_info.health),
            time_remaining: Some(battery_info.time_remaining.into()),
            timestamp: Some(battery_info.timestamp),
        };

        return res;
    }
}

enum StatusUpdateResult {
    Notify,
    DoNotNotify,
}

// General holder struct used to pass around battery info state
// and the associated watchers
// TODO (DNO-686): refactoring will include fixing this, as it
// does not lend itself to scaling across all the facets of battery
// API currently under design.
struct BatteryInfoHelper {
    battery_info: BatteryInfoWrapper,
    watchers: Arc<Mutex<Vec<fpower::BatteryInfoWatcherProxy>>>,
}

#[inline]
fn get_current_time() -> i64 {
    let t = zx::Time::get(zx::ClockId::UTC);
    (t.into_nanos() / 1000) as i64
}

#[derive(Debug)]
enum WatchSuccess {
    Completed,
    BatteryAlreadyFound,
    AdapterAlreadyFound,
}

impl BatteryInfoHelper {
    pub fn new() -> BatteryInfoHelper {
        BatteryInfoHelper {
            battery_info: BatteryInfoWrapper::default(),
            watchers: Arc::new(Mutex::new(Vec::new())),
        }
    }

    // Adds watcher
    fn add_watcher(&mut self, watcher: fpower::BatteryInfoWatcherProxy) {
        self.watchers.lock().push(watcher)
    }

    // Updates the status
    fn update_status(&mut self, file: &File) -> Result<(), failure::Error> {
        let power_info = power::get_power_info(file)?;
        let mut battery_info: Option<hpower::BatteryInfo> = None;
        if power_info.type_ == hpower::PowerType::Battery {
            battery_info = Some(power::get_battery_info(file)?);
        }

        match self.update_battery_info(power_info, battery_info) {
            Ok(StatusUpdateResult::Notify) => {
                let watchers_clone = self.watchers.clone();
                let info_clone = self.get_battery_info_copy();

                BatteryInfoHelper::run_watchers(watchers_clone, info_clone);
            }
            Ok(StatusUpdateResult::DoNotNotify) => {}
            Err(e) => return Err(e),
        }

        Ok(())
    }

    fn run_watchers(
        watchers: Arc<Mutex<Vec<fpower::BatteryInfoWatcherProxy>>>,
        info: BatteryInfoWrapper,
    ) {
        fasync::spawn(async move {
            let watchers = watchers.lock().clone();

            for w in &watchers {
                let _ = w.on_change_battery_info(info.clone().into()).await;
            }
            // TODO (DNO-686): refactoring will include fixing this, which
            // is necessary to clean up the watcher list (retain...) in the
            // event that client connections are closed/dropped.
        })
    }

    fn update_battery_info(
        &mut self,
        power_info: hpower::SourceInfo,
        battery_info: Option<hpower::BatteryInfo>,
    ) -> Result<(StatusUpdateResult), failure::Error> {
        let now = get_current_time();
        let old_battery_info = self.get_battery_info_copy();

        // process new battery info if it is available
        if let Some(bi) = battery_info {
            // general battery status
            self.battery_info.status = fpower::BatteryStatus::Ok;

            // charge status
            if power_info.state & hpower::POWER_STATE_CHARGING != 0 {
                self.battery_info.charge_status = fpower::ChargeStatus::Charging;
            } else if power_info.state & hpower::POWER_STATE_DISCHARGING != 0 {
                self.battery_info.charge_status = fpower::ChargeStatus::Discharging;
            } else {
                self.battery_info.charge_status = fpower::ChargeStatus::NotCharging;
            }

            if bi.remaining_capacity == bi.last_full_capacity {
                self.battery_info.charge_status = fpower::ChargeStatus::Full;
            }

            // charge source
            if self.battery_info.charge_status == fpower::ChargeStatus::Charging
                || self.battery_info.charge_status == fpower::ChargeStatus::Full
            {
                if power_info.type_ == hpower::PowerType::Ac {
                    self.battery_info.charge_source = fpower::ChargeSource::AcAdapter;
                } else {
                    //TODO: how to detect USB/Wireless
                    self.battery_info.charge_source = fpower::ChargeSource::Unknown;
                }
            } else {
                self.battery_info.charge_source = fpower::ChargeSource::None;
            }

            // level percent
            self.battery_info.level_percent =
                (bi.remaining_capacity * 100) as f32 / bi.last_full_capacity as f32;

            // level_status
            if power_info.state & hpower::POWER_STATE_CRITICAL != 0 {
                self.battery_info.level_status = fpower::LevelStatus::Critical;
            } else if bi.remaining_capacity <= bi.capacity_low {
                self.battery_info.level_status = fpower::LevelStatus::Low;
            } else if bi.remaining_capacity <= bi.capacity_warning {
                self.battery_info.level_status = fpower::LevelStatus::Warning;
            } else {
                self.battery_info.level_status = fpower::LevelStatus::Ok;
            }

            // time remaining, provided by hardware as hours
            let nanos_in_one_hour = zx::Duration::from_hours(1);

            if bi.present_rate < 0 {
                // discharging
                let remaining_hours = bi.remaining_capacity as f32 / (bi.present_rate * -1) as f32;
                self.battery_info.time_remaining =
                    TimeRemainingWrapper(fpower::TimeRemaining::BatteryLife(
                        remaining_hours as i64 * nanos_in_one_hour.into_nanos(),
                    ));
            } else {
                // charging
                let remaining_hours = (bi.last_full_capacity as f32 - bi.remaining_capacity as f32)
                    / (bi.present_rate) as f32;
                self.battery_info.time_remaining =
                    TimeRemainingWrapper(fpower::TimeRemaining::FullCharge(
                        remaining_hours as i64 * nanos_in_one_hour.into_nanos(),
                    ));
            }

            // TODO: determine actual battery health
            self.battery_info.health = fpower::HealthStatus::Unknown;
        } else {
            // without battery info, we can still see if battery is online
            // via general power info.
            if power_info.type_ == hpower::PowerType::Battery {
                if power_info.state & hpower::POWER_STATE_ONLINE != 0 {
                    self.battery_info.status = fpower::BatteryStatus::Ok;
                } else {
                    self.battery_info.status = fpower::BatteryStatus::NotAvailable;
                }
            } else {
                self.battery_info.status = fpower::BatteryStatus::Unknown;
            }
            self.battery_info.charge_status = fpower::ChargeStatus::Unknown;
            self.battery_info.health = fpower::HealthStatus::Unknown;
            self.battery_info.time_remaining =
                TimeRemainingWrapper(fpower::TimeRemaining::Indeterminate(0));
        }

        match old_battery_info == self.battery_info {
            true => Ok(StatusUpdateResult::DoNotNotify),
            false => {
                self.battery_info.timestamp = now;
                Ok(StatusUpdateResult::Notify)
            }
        }
    }

    fn get_battery_info_copy(&self) -> BatteryInfoWrapper {
        return BatteryInfoWrapper { ..self.battery_info.clone() };
    }
}

struct PowerManagerServer {
    battery_info_helper: Arc<Mutex<BatteryInfoHelper>>,
}

fn process_watch_event(
    filepath: &PathBuf,
    bsh: Arc<Mutex<BatteryInfoHelper>>,
    battery_device_found: &mut bool,
    adapter_device_found: &mut bool,
) -> Result<WatchSuccess, failure::Error> {
    let file = File::open(&filepath)?;
    let power_info = power::get_power_info(&file)?;

    if power_info.type_ == hpower::PowerType::Battery && *battery_device_found {
        return Ok(WatchSuccess::BatteryAlreadyFound);
    } else if power_info.type_ == hpower::PowerType::Ac && *adapter_device_found {
        return Ok(WatchSuccess::AdapterAlreadyFound);
    }
    let bsh2 = bsh.clone();
    power::add_listener(&file, move |file: &File| {
        let mut bsh2 = bsh2.lock();
        if let Err(e) = bsh2.update_status(&file) {
            fx_log_err!("{}", e);
        }
    })
    .context("adding listener")?;
    {
        let mut bsh = bsh.lock();
        bsh.update_status(&file).context("adding watch events")?;
    }

    if power_info.type_ == hpower::PowerType::Battery {
        *battery_device_found = true;
        let bsh = bsh.clone();
        let mut timer = fasync::Interval::new(zx::Duration::from_seconds(SLEEP_TIME));
        fasync::spawn(async move {
            while let Some(()) = (timer.next()).await {
                let mut bsh = bsh.lock();
                if let Err(e) = bsh.update_status(&file) {
                    fx_log_err!("{}", e);
                }
            }
        });
    } else {
        *adapter_device_found = true;
    }
    Ok(WatchSuccess::Completed)
}

async fn watch_power_device(bsh: Arc<Mutex<BatteryInfoHelper>>) -> Result<(), Error> {
    let file = File::open(POWER_DEVICE).context("cannot find power device")?;
    let mut watcher = vfs_watcher::Watcher::new(&file).await?;
    let mut adapter_device_found = false;
    let mut battery_device_found = false;
    while let Some(msg) = (watcher.try_next()).await? {
        if battery_device_found && adapter_device_found {
            continue;
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
                fx_log_info!("Skip '{:?}' as {} device already found", filepath, device_type);
            }
            Err(err) => {
                fx_log_err!("error for file while adding watch event '{:?}': {}", filepath, err);
            }
        }
    }
    Ok(())
}

fn spawn_power_manager(pm: PowerManagerServer, mut stream: fpower::BatteryManagerRequestStream) {
    fasync::spawn(
        async move {
            while let Some(req) = (stream.try_next()).await? {
                match req {
                    fpower::BatteryManagerRequest::GetBatteryInfo { responder, .. } => {
                        let bsh = pm.battery_info_helper.lock();
                        if let Err(e) = responder.send(bsh.get_battery_info_copy().into()) {
                            fx_log_err!("failed to respond with battery info {:?}", e);
                        }
                    }
                    fpower::BatteryManagerRequest::Watch { watcher, .. } => {
                        match watcher.into_proxy() {
                            Err(e) => {
                                fx_log_err!("failed to get watcher proxy {:?}", e);
                            }
                            Ok(w) => {
                                let bsh = pm.battery_info_helper.clone();
                                let info = bsh.lock().get_battery_info_copy().into();
                                match (w.on_change_battery_info(info)).await {
                                    Ok(_) => bsh.lock().add_watcher(w),
                                    Err(e) => fx_log_err!("failed to add watcher: {:?}", e),
                                };
                            }
                        }
                    }
                }
            }
            Ok(())
        }
            .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
    );
}

fn main() {
    syslog::init_with_tags(&["power_manager"]).expect("Can't init logger");
    fx_log_info!("starting up");
    if let Err(e) = main_pm() {
        fx_log_err!("{:?}", e);
    }
    fx_log_info!("stop");
}

fn main_pm() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("unable to create executor")?;
    let bsh = Arc::new(Mutex::new(BatteryInfoHelper::new()));
    let bsh2 = bsh.clone();
    let f = watch_power_device(bsh2);

    fasync::spawn(f.unwrap_or_else(|e| {
        fx_log_err!("watch_power_device failed {:?}", e);
    }));

    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        let pm = PowerManagerServer { battery_info_helper: bsh.clone() };
        spawn_power_manager(pm, stream);
    });
    fs.take_and_serve_directory_handle()?;
    Ok(executor.run(fs.collect(), 2)) // 2 threads
}

#[cfg(test)]
mod tests {
    use super::*;

    macro_rules! cmp_fields {
        ($got:ident, $want:ident, [$($field:ident,)*], $test_no:expr) => { $(
            assert_eq!($got.$field, $want.$field, "test no: {}", $test_no);
        )* }
    }

    fn check_status(
        got: &BatteryInfoWrapper,
        want: &BatteryInfoWrapper,
        updated: bool,
        test_no: u32,
    ) {
        if updated {
            cmp_fields!(
                want,
                got,
                [
                    status,
                    charge_status,
                    charge_source,
                    level_percent,
                    level_status,
                    health,
                    time_remaining,
                ],
                test_no
            );
        } else {
            assert_eq!(want, got, "test: {}", test_no);
        }
    }

    fn get_default_battery_info() -> hpower::BatteryInfo {
        let battery_info = hpower::BatteryInfo {
            unit: hpower::BatteryUnit::Ma,
            design_capacity: 5000,
            last_full_capacity: 5000,
            design_voltage: 7000,
            capacity_warning: 700,
            capacity_low: 500,
            capacity_granularity_low_warning: 1,
            capacity_granularity_warning_full: 1,
            present_rate: -500,
            remaining_capacity: 3000,
            present_voltage: 7000,
        };
        return battery_info;
    }

    #[test]
    fn update_battery_info() {
        let nanos_in_one_hour = zx::Duration::from_hours(1);

        let mut bsh = BatteryInfoHelper::new();
        let mut power_info = hpower::SourceInfo { type_: hpower::PowerType::Ac, state: 1 };
        // state: ac powered, with no battery info to update
        let mut want = bsh.get_battery_info_copy();
        want.status = fpower::BatteryStatus::Unknown;
        want.charge_source = fpower::ChargeSource::Unknown;
        let _ = bsh.update_battery_info(power_info.clone(), None);
        check_status(&bsh.battery_info, &want, true, 1);

        // state: unchanged
        let want = bsh.get_battery_info_copy();
        let _ = bsh.update_battery_info(power_info.clone(), None);
        check_status(&bsh.battery_info, &want, false, 2);

        // state: battery powered, discharging
        power_info.type_ = hpower::PowerType::Battery;
        power_info.state = 0x3; // ONLINE | DISCHARGING
        let mut want = bsh.get_battery_info_copy();
        want.status = fpower::BatteryStatus::Ok;
        want.charge_status = fpower::ChargeStatus::Discharging;
        want.charge_source = fpower::ChargeSource::None;
        want.level_status = fpower::LevelStatus::Ok;
        want.level_percent = (3000.0 * 100.0) / 5000.0;
        want.time_remaining = TimeRemainingWrapper(fpower::TimeRemaining::BatteryLife(
            6 * nanos_in_one_hour.into_nanos(),
        ));
        let mut battery_info = get_default_battery_info();
        let _ = bsh.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_info, &want, true, 3);

        // state: battery powered, discharging/warning
        power_info.state = 0x3; // ONLINE | DISCHARGING
        battery_info.remaining_capacity = 700;
        let mut want = bsh.get_battery_info_copy();
        want.status = fpower::BatteryStatus::Ok;
        want.charge_status = fpower::ChargeStatus::Discharging;
        want.charge_source = fpower::ChargeSource::None;
        want.level_status = fpower::LevelStatus::Warning;
        want.level_percent = (700.0 * 100.0) / 5000.0;
        want.time_remaining = TimeRemainingWrapper(fpower::TimeRemaining::BatteryLife(
            nanos_in_one_hour.into_nanos(),
        ));
        let _ = bsh.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_info, &want, true, 4);

        // state: battery powered, discharging/low
        power_info.state = 0x3; // ONLINE | DISCHARGING
        battery_info.remaining_capacity = 500;
        let mut want = bsh.get_battery_info_copy();
        want.status = fpower::BatteryStatus::Ok;
        want.charge_status = fpower::ChargeStatus::Discharging;
        want.charge_source = fpower::ChargeSource::None;
        want.level_status = fpower::LevelStatus::Low;
        want.level_percent = (500.0 * 100.0) / 5000.0;
        want.time_remaining = TimeRemainingWrapper(fpower::TimeRemaining::BatteryLife(
            nanos_in_one_hour.into_nanos(),
        ));
        let _ = bsh.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_info, &want, true, 5);

        // state: battery powered, discharging/critical
        power_info.state = 0xB; // ONLINE | DISCHARGING | CRITICAL
        let mut want = bsh.get_battery_info_copy();
        want.status = fpower::BatteryStatus::Ok;
        want.charge_status = fpower::ChargeStatus::Discharging;
        want.charge_source = fpower::ChargeSource::None;
        want.level_status = fpower::LevelStatus::Critical;
        let _ = bsh.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_info, &want, true, 6);

        // state: ac powered, charging
        power_info.type_ = hpower::PowerType::Ac;
        power_info.state = 0x5; // ONLINE | CHARGING
        battery_info.present_rate = 1000;
        battery_info.remaining_capacity = 3000;
        let mut want = bsh.get_battery_info_copy();
        want.status = fpower::BatteryStatus::Ok;
        want.charge_status = fpower::ChargeStatus::Charging;
        want.charge_source = fpower::ChargeSource::AcAdapter;
        want.level_status = fpower::LevelStatus::Ok;
        want.level_percent = (3000.0 * 100.0) / 5000.0;
        want.time_remaining = TimeRemainingWrapper(fpower::TimeRemaining::FullCharge(
            2 * nanos_in_one_hour.into_nanos(),
        ));
        let _ = bsh.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_info, &want, true, 7);

        // state: ac powered, charging/critical
        power_info.state = 0xD; // ONLINE | CHARGING | CRITICAL
        let mut want = bsh.get_battery_info_copy();
        want.status = fpower::BatteryStatus::Ok;
        want.charge_status = fpower::ChargeStatus::Charging;
        want.charge_source = fpower::ChargeSource::AcAdapter;
        want.level_status = fpower::LevelStatus::Critical;
        let _ = bsh.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_info, &want, true, 8);

        // state: ac powered, charging/full
        power_info.state = 0x5; // ONLINE | CHARGING
        battery_info.remaining_capacity = 5000;
        let mut want = bsh.get_battery_info_copy();
        want.status = fpower::BatteryStatus::Ok;
        want.charge_status = fpower::ChargeStatus::Full;
        want.charge_source = fpower::ChargeSource::AcAdapter;
        want.level_status = fpower::LevelStatus::Ok;
        want.level_percent = 100.0;
        want.time_remaining = TimeRemainingWrapper(fpower::TimeRemaining::FullCharge(0));
        let _ = bsh.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&bsh.battery_info, &want, true, 9);
    }
}
