// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::LOG_VERBOSITY,
    anyhow::Error,
    fidl::endpoints::Proxy,
    fidl_fuchsia_hardware_power as hpower, fidl_fuchsia_power as fpower,
    fidl_fuchsia_power_ext::CloneExt,
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_vlog},
    fuchsia_zircon as zx,
    futures::{lock::Mutex, TryStreamExt},
    std::sync::{Arc, RwLock},
};

#[derive(Debug, PartialEq)]
enum StatusUpdateResult {
    Notify,
    DoNotNotify,
}

pub(crate) trait BatterySimulationStateObserver {
    fn update_simulation(&self, new_state: bool);
    fn update_simulated_battery_info(&self, battery_info: fpower::BatteryInfo);
}

impl BatterySimulationStateObserver for BatteryManager {
    fn update_simulation(&self, is_simulating: bool) {
        let mut sim_state = self.simulation_state.write().unwrap();
        *sim_state = is_simulating;
        drop(sim_state);
        if !is_simulating {
            self.update_watchers();
        }
    }
    fn update_simulated_battery_info(&self, battery_info: fpower::BatteryInfo) {
        let mut simulated_battery_info = self.simulated_battery_info.write().unwrap();
        *simulated_battery_info = battery_info.clone();
        drop(simulated_battery_info);
        self.update_watchers();
    }
}

/// Core component for the battery manager system.
///
/// BatteryManager maintains the current state info for the battery system
/// as well as the watchers that share this information with subscribed clients.
///
/// simulation_state: true when the simulator is running
pub struct BatteryManager {
    battery_info: RwLock<fpower::BatteryInfo>,
    watchers: Arc<Mutex<Vec<fpower::BatteryInfoWatcherProxy>>>,
    simulation_state: RwLock<bool>,
    simulated_battery_info: RwLock<fpower::BatteryInfo>,
}

#[inline]
fn get_current_time() -> i64 {
    let t = zx::Time::get(zx::ClockId::UTC);
    (t.into_nanos() / 1000) as i64
}

impl BatteryManager {
    pub fn new() -> BatteryManager {
        BatteryManager {
            battery_info: RwLock::new(fpower::BatteryInfo {
                status: Some(fpower::BatteryStatus::NotAvailable),
                charge_status: Some(fpower::ChargeStatus::Unknown),
                charge_source: Some(fpower::ChargeSource::Unknown),
                level_percent: None,
                level_status: Some(fpower::LevelStatus::Unknown),
                health: Some(fpower::HealthStatus::Unknown),
                time_remaining: Some(fpower::TimeRemaining::Indeterminate(0)),
                timestamp: Some(get_current_time()),
            }),
            watchers: Arc::new(Mutex::new(Vec::new())),
            simulation_state: RwLock::new(false),
            simulated_battery_info: RwLock::new(fpower::BatteryInfo {
                status: Some(fpower::BatteryStatus::NotAvailable),
                charge_status: Some(fpower::ChargeStatus::Unknown),
                charge_source: Some(fpower::ChargeSource::Unknown),
                level_percent: None,
                level_status: Some(fpower::LevelStatus::Unknown),
                health: Some(fpower::HealthStatus::Unknown),
                time_remaining: Some(fpower::TimeRemaining::Indeterminate(0)),
                timestamp: Some(get_current_time()),
            }),
        }
    }

    // Adds watcher
    pub async fn add_watcher(&self, watcher: fpower::BatteryInfoWatcherProxy) {
        let mut watchers = self.watchers.lock().await;
        fx_vlog!(LOG_VERBOSITY, "::manager:: adding watcher: {:?} [{:?}]", watcher, watchers.len());
        watchers.push(watcher)
    }

    // Updates the status
    pub fn update_status(
        &self,
        power_info: hpower::SourceInfo,
        battery_info: Option<hpower::BatteryInfo>,
    ) -> Result<(), anyhow::Error> {
        fx_vlog!(
            LOG_VERBOSITY,
            "update_status with power info: {:#?} and battery info: {:#?}",
            &power_info,
            &battery_info
        );

        match self.update_battery_info(power_info, battery_info) {
            Ok(StatusUpdateResult::Notify) => {
                fx_vlog!(LOG_VERBOSITY, "::manager:: update status changed - NOTIFY");
                let info = self.get_battery_info_copy();
                let watchers = self.watchers.clone();
                fx_vlog!(
                    LOG_VERBOSITY,
                    "::manager:: run watchers {:?} with info {:?}",
                    &watchers,
                    &info
                );
                BatteryManager::run_watchers(watchers.clone(), info.clone());
            }
            Ok(StatusUpdateResult::DoNotNotify) => {
                fx_vlog!(LOG_VERBOSITY, "::manager:: update status unchanged - skipping NOTIFY");
            }
            Err(e) => return Err(e),
        }

        Ok(())
    }

    pub fn run_watchers(
        watchers: Arc<Mutex<Vec<fpower::BatteryInfoWatcherProxy>>>,
        info: fpower::BatteryInfo,
    ) {
        fx_vlog!(LOG_VERBOSITY, "::manager:: run watchers...");
        fasync::Task::spawn(async move {
            let watchers = {
                let mut watchers = watchers.lock().await;
                watchers.retain(|w| !w.is_closed());
                watchers.clone()
            };
            fx_vlog!(LOG_VERBOSITY, "::manager:: run watchers [{:?}]", &watchers.len());
            for w in &watchers {
                if let Err(e) = w.on_change_battery_info(info.clone().into()).await {
                    fx_log_err!("failed to send battery info to watcher {:?}", e);
                }
            }
        })
        .detach()
    }

    fn update_battery_info(
        &self,
        power_info: hpower::SourceInfo,
        battery_info: Option<hpower::BatteryInfo>,
    ) -> Result<StatusUpdateResult, anyhow::Error> {
        let now = get_current_time();
        let old_battery_info = self.get_battery_info_copy();
        fx_vlog!(LOG_VERBOSITY, "::battery_manager:: old battery info: {:?}", &old_battery_info);

        let mut new_battery_info = self.battery_info.write().unwrap();

        // info from AC power source
        if power_info.type_ == hpower::PowerType::Ac {
            // charge status/source
            if power_info.state & hpower::POWER_STATE_CHARGING != 0 {
                new_battery_info.charge_status = Some(fpower::ChargeStatus::Charging);
                if power_info.type_ == hpower::PowerType::Ac {
                    new_battery_info.charge_source = Some(fpower::ChargeSource::AcAdapter);
                } else {
                    //TODO: how to detect USB/Wireless
                    new_battery_info.charge_source = Some(fpower::ChargeSource::Unknown);
                }
            }
        }

        // info from battery power source will include battery info
        if let Some(bi) = battery_info {
            assert!(
                power_info.type_ == hpower::PowerType::Battery,
                "updating battery info with non-battery power source"
            );

            fx_vlog!(
                LOG_VERBOSITY,
                "::battery_manager:: update with hpower info p:{:?} b:{:?}",
                &power_info,
                &bi
            );

            // check battery online and update accordingly
            if power_info.state & hpower::POWER_STATE_ONLINE != 0 {
                new_battery_info.status = Some(fpower::BatteryStatus::Ok);

                // charge status/source
                if power_info.state & hpower::POWER_STATE_CHARGING != 0 {
                    new_battery_info.charge_status = Some(fpower::ChargeStatus::Charging);
                }

                if bi.remaining_capacity == bi.last_full_capacity {
                    new_battery_info.charge_status = Some(fpower::ChargeStatus::Full);
                }

                // level percent
                new_battery_info.level_percent = Some(
                    (bi.remaining_capacity.saturating_mul(100)) as f32
                        / bi.last_full_capacity as f32,
                );

                // level_status
                if power_info.state & hpower::POWER_STATE_CRITICAL != 0 {
                    new_battery_info.level_status = Some(fpower::LevelStatus::Critical);
                } else if bi.remaining_capacity <= bi.capacity_low {
                    new_battery_info.level_status = Some(fpower::LevelStatus::Low);
                } else if bi.remaining_capacity <= bi.capacity_warning {
                    new_battery_info.level_status = Some(fpower::LevelStatus::Warning);
                } else {
                    new_battery_info.level_status = Some(fpower::LevelStatus::Ok);
                }

                // time remaining, provided by hardware as hours
                let nanos_in_one_hour = zx::Duration::from_hours(1);

                if bi.present_rate < 0 {
                    // discharging
                    let remaining_hours =
                        bi.remaining_capacity as f32 / (bi.present_rate.saturating_mul(-1)) as f32;
                    new_battery_info.time_remaining = Some(fpower::TimeRemaining::BatteryLife(
                        (remaining_hours as i64).saturating_mul(nanos_in_one_hour.into_nanos()),
                    ));
                } else {
                    // charging
                    let remaining_hours = (bi.last_full_capacity as f32
                        - bi.remaining_capacity as f32)
                        / (bi.present_rate) as f32;
                    new_battery_info.time_remaining = Some(fpower::TimeRemaining::FullCharge(
                        (remaining_hours as i64).saturating_mul(nanos_in_one_hour.into_nanos()),
                    ));
                }

                // TODO: determine actual battery health
                new_battery_info.health = Some(fpower::HealthStatus::Unknown);
            } else {
                // battery offline/not present
                new_battery_info.status = Some(fpower::BatteryStatus::NotPresent);
            }
        }

        if power_info.state & hpower::POWER_STATE_DISCHARGING != 0 {
            new_battery_info.charge_status = Some(fpower::ChargeStatus::Discharging);
        }

        if (power_info.state & hpower::POWER_STATE_DISCHARGING == 0)
            && (power_info.state & hpower::POWER_STATE_CHARGING == 0)
        {
            new_battery_info.charge_status = Some(fpower::ChargeStatus::NotCharging);
        }

        if new_battery_info.charge_status != Some(fpower::ChargeStatus::Charging)
            && new_battery_info.charge_status != Some(fpower::ChargeStatus::Full)
        {
            new_battery_info.charge_source = Some(fpower::ChargeSource::None);
        }

        if *self.simulation_state.read().unwrap() {
            return Ok(StatusUpdateResult::DoNotNotify);
        }

        match old_battery_info == (*new_battery_info) {
            true => Ok(StatusUpdateResult::DoNotNotify),
            false => {
                new_battery_info.timestamp = Some(now);
                Ok(StatusUpdateResult::Notify)
            }
        }
    }

    pub fn get_battery_info_copy(&self) -> fpower::BatteryInfo {
        if *self.simulation_state.read().unwrap() {
            let info_lock = self.simulated_battery_info.read().unwrap();
            (*info_lock).clone()
        } else {
            let info_lock = self.battery_info.read().unwrap();
            (*info_lock).clone()
        }
    }

    fn update_watchers(&self) {
        let info = self.get_battery_info_copy();
        let watchers = self.watchers.clone();
        BatteryManager::run_watchers(watchers.clone(), info.clone());
    }

    pub fn is_simulating(&self) -> bool {
        *self.simulation_state.read().unwrap()
    }

    pub(crate) async fn serve(
        &self,
        stream: fpower::BatteryManagerRequestStream,
    ) -> Result<(), Error> {
        stream.try_for_each_concurrent(None, move |request| {
            async move {
                match request {
                    fpower::BatteryManagerRequest::GetBatteryInfo { responder, .. } => {
                        let info = self.get_battery_info_copy();
                        fx_vlog!(
                            LOG_VERBOSITY,
                            "::battery_manager_request:: handle GetBatteryInfo request with info: {:?}",
                            &info
                        );
                        responder.send(info)?;

                    }
                    fpower::BatteryManagerRequest::Watch { watcher, .. } => {
                        let watcher = watcher.into_proxy()?;
                        fx_vlog!(LOG_VERBOSITY, "::battery_manager_request:: handle Watch request");
                        self.add_watcher(watcher.clone()).await;

                        // make sure watcher has current battery info
                        let info = self.get_battery_info_copy();

                        fx_vlog!(
                            LOG_VERBOSITY,
                            "::battery_manager_request:: callback on new watcher with info {:?}",
                            &info
                        );
                        watcher.on_change_battery_info(info).await?;
                    }
                }
                Ok(())
            }
        })
        .await?;

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_request_stream;
    use fidl_fuchsia_power as fpower;
    use fuchsia_async::futures::TryStreamExt;
    use futures::future::*;

    macro_rules! cmp_fields {
        ($got:ident, $want:ident, [$($field:ident,)*], $test_no:expr) => { $(
            assert_eq!($got.$field, $want.$field, "test no: {}", $test_no);
        )* }
    }

    fn check_status(
        got: &fpower::BatteryInfo,
        want: &fpower::BatteryInfo,
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

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_run_watcher() {
        let battery_manager = BatteryManager::new();
        let mut battery_info: fpower::BatteryInfo = battery_manager.get_battery_info_copy();
        battery_info.level_percent = Some(50.0);

        let (watcher_client_end, mut stream) =
            create_request_stream::<fpower::BatteryInfoWatcherMarker>().unwrap();
        let watcher = watcher_client_end.into_proxy().unwrap();

        let watchers = Arc::new(Mutex::new(vec![watcher]));

        let serve_fut = async move {
            let request = stream.try_next().await.unwrap();
            if let Some(fpower::BatteryInfoWatcherRequest::OnChangeBatteryInfo {
                info,
                responder,
            }) = request
            {
                let level = info.level_percent.unwrap().round() as u8;
                assert_eq!(level, 50);
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
        };
        let request_fut = async move {
            BatteryManager::run_watchers(watchers, battery_info);
        };

        join(serve_fut, request_fut).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_run_watchers_channel_closed() {
        let battery_manager = BatteryManager::new();
        let mut battery_info: fpower::BatteryInfo = battery_manager.get_battery_info_copy();
        battery_info.level_percent = Some(50.0);

        let (watcher1_client_end, mut stream1) =
            create_request_stream::<fpower::BatteryInfoWatcherMarker>().unwrap();
        let watcher1 = watcher1_client_end.into_proxy().unwrap();

        let (watcher2_client_end, mut stream2) =
            create_request_stream::<fpower::BatteryInfoWatcherMarker>().unwrap();
        let watcher2 = watcher2_client_end.into_proxy().unwrap();

        let watchers = Arc::new(Mutex::new(vec![watcher1, watcher2]));

        let serve1_fut = async move {
            // first request should match first change notification sent
            // at 50%
            let request = stream1.try_next().await.unwrap();
            if let Some(fpower::BatteryInfoWatcherRequest::OnChangeBatteryInfo {
                info,
                responder,
            }) = request
            {
                let level = info.level_percent.unwrap().round() as u8;
                assert_eq!(level, 50);
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
            // second should match subsequent notification at 60%
            let request = stream1.try_next().await.unwrap();
            if let Some(fpower::BatteryInfoWatcherRequest::OnChangeBatteryInfo {
                info,
                responder,
            }) = request
            {
                let level = info.level_percent.unwrap().round() as u8;
                assert_eq!(level, 60);
                responder.send().unwrap();
            } else {
                panic!("Unexpected message received");
            };
        };

        let serve2_fut = async move {
            // first request should match first change notification sent
            // at 50%
            let request = stream2.try_next().await.unwrap();
            if let Some(fpower::BatteryInfoWatcherRequest::OnChangeBatteryInfo {
                info,
                responder,
            }) = request
            {
                let level = info.level_percent.unwrap().round() as u8;
                assert_eq!(level, 50);
                // but then we drop the channel...
                std::mem::drop(responder);
            } else {
                panic!("Unexpected message received");
            };
            // should not get the second...
            if let Some(_) = stream2.try_next().await.unwrap() {
                panic!("Unexpected message, channel should be closed");
            }
        };

        let request_fut = async move {
            BatteryManager::run_watchers(watchers.clone(), battery_info.clone());
            battery_info.level_percent = Some(60.0);
            BatteryManager::run_watchers(watchers, battery_info);
        };

        join3(serve1_fut, serve2_fut, request_fut).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn update_battery_info() {
        let nanos_in_one_hour = zx::Duration::from_hours(1);

        let battery_manager = BatteryManager::new();
        let mut power_info = hpower::SourceInfo { type_: hpower::PowerType::Ac, state: 1 };

        // state: ac powered, with no battery info to update
        let mut want = battery_manager.get_battery_info_copy();
        want.status = Some(fpower::BatteryStatus::NotAvailable);
        want.charge_source = Some(fpower::ChargeSource::None);
        want.charge_status = Some(fpower::ChargeStatus::NotCharging);
        want.level_percent = None;
        let _ = battery_manager.update_battery_info(power_info.clone(), None);
        check_status(&battery_manager.get_battery_info_copy(), &want, true, 1);

        // state: unchanged
        let want = battery_manager.get_battery_info_copy();
        let result = battery_manager.update_battery_info(power_info.clone(), None);
        assert_eq!(result.unwrap(), StatusUpdateResult::DoNotNotify);
        check_status(&battery_manager.get_battery_info_copy(), &want, false, 2);

        // state: battery powered, discharging
        power_info.type_ = hpower::PowerType::Battery;
        power_info.state = 0x3; // ONLINE | DISCHARGING
        let mut want = battery_manager.get_battery_info_copy();
        want.status = Some(fpower::BatteryStatus::Ok);
        want.charge_status = Some(fpower::ChargeStatus::Discharging);
        want.charge_source = Some(fpower::ChargeSource::None);
        want.level_status = Some(fpower::LevelStatus::Ok);
        want.level_percent = Some((3000.0 * 100.0) / 5000.0);
        want.time_remaining =
            Some(fpower::TimeRemaining::BatteryLife(6 * nanos_in_one_hour.into_nanos()));
        let mut battery_info = get_default_battery_info();
        let result =
            battery_manager.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        assert_eq!(result.unwrap(), StatusUpdateResult::Notify);
        check_status(&battery_manager.get_battery_info_copy(), &want, true, 3);

        // state: battery powered, discharging/warning
        power_info.state = 0x3; // ONLINE | DISCHARGING
        battery_info.remaining_capacity = 700;
        let mut want = battery_manager.get_battery_info_copy();
        want.status = Some(fpower::BatteryStatus::Ok);
        want.charge_status = Some(fpower::ChargeStatus::Discharging);
        want.charge_source = Some(fpower::ChargeSource::None);
        want.level_status = Some(fpower::LevelStatus::Warning);
        want.level_percent = Some((700.0 * 100.0) / 5000.0);
        want.time_remaining =
            Some(fpower::TimeRemaining::BatteryLife(nanos_in_one_hour.into_nanos()));
        let _ = battery_manager.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&battery_manager.get_battery_info_copy(), &want, true, 4);

        // state: battery powered, discharging/low
        power_info.state = 0x3; // ONLINE | DISCHARGING
        battery_info.remaining_capacity = 500;
        let mut want = battery_manager.get_battery_info_copy();
        want.status = Some(fpower::BatteryStatus::Ok);
        want.charge_status = Some(fpower::ChargeStatus::Discharging);
        want.charge_source = Some(fpower::ChargeSource::None);
        want.level_status = Some(fpower::LevelStatus::Low);
        want.level_percent = Some((500.0 * 100.0) / 5000.0);
        want.time_remaining =
            Some(fpower::TimeRemaining::BatteryLife(nanos_in_one_hour.into_nanos()));
        let _ = battery_manager.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&battery_manager.get_battery_info_copy(), &want, true, 5);

        // state: battery powered, discharging/critical
        power_info.state = 0xB; // ONLINE | DISCHARGING | CRITICAL
        let mut want = battery_manager.get_battery_info_copy();
        want.status = Some(fpower::BatteryStatus::Ok);
        want.charge_status = Some(fpower::ChargeStatus::Discharging);
        want.charge_source = Some(fpower::ChargeSource::None);
        want.level_status = Some(fpower::LevelStatus::Critical);
        let _ = battery_manager.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&battery_manager.get_battery_info_copy(), &want, true, 6);

        // state: battery charging via AC
        power_info.type_ = hpower::PowerType::Ac;
        power_info.state = 0x5; // ONLINE | CHARGING
        let _ = battery_manager.update_battery_info(power_info.clone(), None);
        power_info.type_ = hpower::PowerType::Battery;
        power_info.state = 0x5; // ONLINE | CHARGING
        battery_info.present_rate = 1000;
        battery_info.remaining_capacity = 3000;
        let mut want = battery_manager.get_battery_info_copy();
        want.status = Some(fpower::BatteryStatus::Ok);
        want.charge_status = Some(fpower::ChargeStatus::Charging);
        want.charge_source = Some(fpower::ChargeSource::AcAdapter);
        want.level_status = Some(fpower::LevelStatus::Ok);
        want.level_percent = Some((3000.0 * 100.0) / 5000.0);
        want.time_remaining =
            Some(fpower::TimeRemaining::FullCharge(2 * nanos_in_one_hour.into_nanos()));
        let _ = battery_manager.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&battery_manager.get_battery_info_copy(), &want, true, 7);

        // state: battery charging via AC/level critical
        power_info.type_ = hpower::PowerType::Ac;
        power_info.state = 0xD; // ONLINE | CHARGING | CRITICAL
        let _ = battery_manager.update_battery_info(power_info.clone(), None);
        power_info.type_ = hpower::PowerType::Battery;
        power_info.state = 0xD; // ONLINE | CHARGING | CRITICAL
        let mut want = battery_manager.get_battery_info_copy();
        want.status = Some(fpower::BatteryStatus::Ok);
        want.charge_status = Some(fpower::ChargeStatus::Charging);
        want.charge_source = Some(fpower::ChargeSource::AcAdapter);
        want.level_status = Some(fpower::LevelStatus::Critical);
        let _ = battery_manager.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&battery_manager.get_battery_info_copy(), &want, true, 8);

        // state: battery charging via AC/level full
        power_info.state = 0x5; // ONLINE | CHARGING
        battery_info.remaining_capacity = 5000;
        let mut want = battery_manager.get_battery_info_copy();
        want.status = Some(fpower::BatteryStatus::Ok);
        want.charge_status = Some(fpower::ChargeStatus::Full);
        want.charge_source = Some(fpower::ChargeSource::AcAdapter);
        want.level_status = Some(fpower::LevelStatus::Ok);
        want.level_percent = Some(100.0);
        want.time_remaining = Some(fpower::TimeRemaining::FullCharge(0));
        let _ = battery_manager.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&battery_manager.get_battery_info_copy(), &want, true, 9);

        // state: battery charging via AC/extreme values (check overflow)
        power_info.state = 0x5; // ONLINE | CHARGING
        battery_info.last_full_capacity = u32::max_value();
        battery_info.remaining_capacity = u32::min_value();
        battery_info.present_rate = 1;
        let mut want = battery_manager.get_battery_info_copy();
        want.status = Some(fpower::BatteryStatus::Ok);
        want.charge_status = Some(fpower::ChargeStatus::Charging);
        want.charge_source = Some(fpower::ChargeSource::AcAdapter);
        want.level_status = Some(fpower::LevelStatus::Low);
        want.level_percent = Some(0.0);
        want.time_remaining = Some(fpower::TimeRemaining::FullCharge(i64::max_value()));
        let _ = battery_manager.update_battery_info(power_info.clone(), Some(battery_info.clone()));
        check_status(&battery_manager.get_battery_info_copy(), &want, true, 10);
    }
}
