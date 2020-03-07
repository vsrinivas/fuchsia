// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::battery_manager::BatterySimulationStateObserver,
    anyhow::{format_err, Error},
    fidl_fuchsia_power as fpower,
    fidl_fuchsia_power_ext::CloneExt,
    fidl_fuchsia_power_test as spower,
    futures::lock::Mutex,
    std::sync::{Arc, Weak},
};

// SimulatedBatteryInfoSource is another source of BatteryInfo for the
// BatteryManager. This BatteryInfo is modified through FIDL calls
pub(crate) struct SimulatedBatteryInfoSource {
    battery_info: Arc<Mutex<fpower::BatteryInfo>>,
    observer: Weak<dyn BatterySimulationStateObserver>,
}

impl SimulatedBatteryInfoSource {
    pub fn new(
        battery_info: fpower::BatteryInfo,
        observer: Weak<dyn BatterySimulationStateObserver>,
    ) -> Self {
        Self { battery_info: Arc::new(Mutex::new(battery_info)), observer }
    }

    pub async fn get_battery_info_copy(&self) -> fpower::BatteryInfo {
        let battery_info = self.battery_info.lock().await;
        (*battery_info).clone()
    }

    async fn set_battery_percentage(&self, level_percent: f32) -> Result<(), Error> {
        let mut battery_info = self.battery_info.lock().await;
        battery_info.level_percent = Some(level_percent);
        drop(battery_info);
        self.notify_battery_info_changed().await?;
        Ok(())
    }

    async fn set_charge_source(&self, charge_source: fpower::ChargeSource) -> Result<(), Error> {
        let mut battery_info = self.battery_info.lock().await;
        battery_info.charge_source = Some(charge_source);
        drop(battery_info);
        self.notify_battery_info_changed().await?;
        Ok(())
    }

    async fn set_level_status(&self, level_status: fpower::LevelStatus) -> Result<(), Error> {
        let mut battery_info = self.battery_info.lock().await;
        battery_info.level_status = Some(level_status);
        drop(battery_info);
        self.notify_battery_info_changed().await?;
        Ok(())
    }

    async fn set_charge_status(&self, charge_status: fpower::ChargeStatus) -> Result<(), Error> {
        let mut battery_info = self.battery_info.lock().await;
        battery_info.charge_status = Some(charge_status);
        drop(battery_info);
        self.notify_battery_info_changed().await?;
        Ok(())
    }

    async fn set_battery_status(&self, battery_status: fpower::BatteryStatus) -> Result<(), Error> {
        let mut battery_info = self.battery_info.lock().await;
        battery_info.status = Some(battery_status);
        drop(battery_info);
        self.notify_battery_info_changed().await?;
        Ok(())
    }

    async fn set_time_remaining(&self, time: i64) -> Result<(), Error> {
        let mut battery_info = self.battery_info.lock().await;
        if battery_info.charge_status.unwrap() == fpower::ChargeStatus::Charging {
            battery_info.time_remaining = Some(fpower::TimeRemaining::FullCharge(time));
        } else if battery_info.charge_status.unwrap() == fpower::ChargeStatus::Discharging
            || battery_info.charge_status.unwrap() == fpower::ChargeStatus::NotCharging
        {
            battery_info.time_remaining = Some(fpower::TimeRemaining::BatteryLife(time));
        } else {
            battery_info.time_remaining = Some(fpower::TimeRemaining::Indeterminate(time));
        }
        drop(battery_info);
        self.notify_battery_info_changed().await?;
        Ok(())
    }

    // Updates the simulated_battery_info in BatteryManager
    async fn notify_battery_info_changed(&self) -> Result<(), Error> {
        let observer = self.observer.upgrade().ok_or(format_err!("Observer not found"))?;
        observer.update_simulated_battery_info((*self.battery_info.lock().await).clone());
        Ok(())
    }

    // Updates BatteryManager as well as store the most up to date BatteryInfo
    pub async fn update_simulation(
        &self,
        is_simulating: bool,
        real_battery_info: fpower::BatteryInfo,
    ) -> Result<(), Error> {
        let observer = self.observer.upgrade().ok_or(format_err!("Observer not found"))?;
        observer.update_simulation(is_simulating);

        // Copy real_battery_info
        let mut battery_info = self.battery_info.lock().await;
        *battery_info = real_battery_info;
        drop(battery_info);
        self.notify_battery_info_changed().await?;
        Ok(())
    }

    pub(crate) async fn handle_request(
        &self,
        request: spower::BatterySimulatorRequest,
    ) -> Result<(), Error> {
        async move {
            match request {
                spower::BatterySimulatorRequest::GetBatteryInfo { responder, .. } => {
                    let info = self.get_battery_info_copy();
                    responder.send(info.await).map_err(|e| format_err!("{}", e))?;
                }
                spower::BatterySimulatorRequest::SetChargeStatus { charge_status, .. } => {
                    self.set_charge_status(charge_status).await?;
                }
                spower::BatterySimulatorRequest::SetBatteryStatus { battery_status, .. } => {
                    self.set_battery_status(battery_status).await?;
                }
                spower::BatterySimulatorRequest::SetChargeSource { charge_source, .. } => {
                    self.set_charge_source(charge_source).await?;
                }
                spower::BatterySimulatorRequest::SetBatteryPercentage { percent, .. } => {
                    self.set_battery_percentage(percent).await?;
                }
                spower::BatterySimulatorRequest::SetLevelStatus { level_status, .. } => {
                    self.set_level_status(level_status).await?;
                }
                spower::BatterySimulatorRequest::SetTimeRemaining { duration, .. } => {
                    self.set_time_remaining(duration).await?;
                }
                spower::BatterySimulatorRequest::DisconnectRealBattery { .. } => {
                    Err(format_err!("Incorrect Disconnect request called"))?
                }
                spower::BatterySimulatorRequest::ReconnectRealBattery { .. } => {
                    Err(format_err!("Incorrect Reconnect request called"))?
                }
            }
            Ok::<(), Error>(())
        }
        .await?;

        Ok(())
    }
}

// TODO(rminocha): investigate ways to make unit tests linear
#[cfg(test)]
mod tests {
    use super::*;
    use crate::battery_manager::BatteryManager;
    use fidl::endpoints::create_request_stream;
    use fidl_fuchsia_power as fpower;
    use fuchsia_async::futures::TryStreamExt;
    use futures::future::*;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_observer_relationship() {
        let (watcher_client_end, mut stream) =
            create_request_stream::<fpower::BatteryInfoWatcherMarker>().unwrap();
        let watcher = watcher_client_end.into_proxy().unwrap();

        let battery_manager = Arc::new(BatteryManager::new());
        battery_manager.add_watcher(watcher).await;

        let mut battery_info: fpower::BatteryInfo = battery_manager.get_battery_info_copy();
        battery_info.level_percent = Some(50.0);

        let simulated_battery_info_source = SimulatedBatteryInfoSource::new(
            battery_manager.get_battery_info_copy(),
            Arc::downgrade(&battery_manager) as Weak<dyn BatterySimulationStateObserver>,
        );

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

            let request = stream.try_next().await.unwrap();
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

        let request_fut = async move {
            // Update_simulation requires a battery_info since that holds the updated simulated
            // battery info object.
            let res =
                simulated_battery_info_source.update_simulation(true, battery_info.clone()).await;
            assert!(!res.is_err(), "Update simulation failed");
            battery_info.level_percent = Some(60.0);
            let res =
                simulated_battery_info_source.update_simulation(true, battery_info.clone()).await;
            assert!(!res.is_err(), "Update simulation failed");
        };

        join(serve_fut, request_fut).await;
    }
}
