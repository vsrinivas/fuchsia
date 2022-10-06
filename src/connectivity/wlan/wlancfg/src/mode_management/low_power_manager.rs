// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{mode_management::phy_manager::PhyManagerApi, telemetry},
    fidl_fuchsia_power_clientlevel as fidl_power, fidl_fuchsia_wlan_common as fidl_common,
    futures::lock::Mutex,
    log::{info, warn},
    std::sync::Arc,
};

fn power_level_to_mode(level: u64) -> fidl_common::PowerSaveType {
    match level {
        0 => fidl_common::PowerSaveType::PsModeUltraLowPower,
        1 => fidl_common::PowerSaveType::PsModeLowPower,
        2 => fidl_common::PowerSaveType::PsModeBalanced,
        3 => fidl_common::PowerSaveType::PsModePerformance,
        // Any power level that is implied to be higher than performance mode will be mapped to
        // performance mode.
        _ => fidl_common::PowerSaveType::PsModePerformance,
    }
}

pub struct PowerModeManager<P: PhyManagerApi + ?Sized> {
    power_watcher: fidl_power::WatcherProxy,
    phy_manager: Arc<Mutex<P>>,
    telemetry_sender: telemetry::TelemetrySender,
}

impl<P: PhyManagerApi + ?Sized> PowerModeManager<P> {
    pub fn new(
        power_watcher: fidl_power::WatcherProxy,
        phy_manager: Arc<Mutex<P>>,
        telemetry_sender: telemetry::TelemetrySender,
    ) -> Self {
        PowerModeManager { power_watcher, phy_manager, telemetry_sender }
    }

    pub async fn run(&self) {
        let mut current_power_mode = None;

        loop {
            // Per the API specification, if a system does not have a WLAN power mode
            // configuration, it will simply drop the channel.
            let power_level = match self.power_watcher.watch().await {
                Ok(level) => level,
                Err(e) => {
                    warn!("Error while waiting for low power state updates: {:?}", e);
                    return;
                }
            };

            let power_mode = power_level_to_mode(power_level);

            info!("Received power level {}, setting power mode {:?}", power_level, power_mode);

            let mut phy_manager = self.phy_manager.lock().await;
            if let Err(e) = phy_manager.set_power_state(power_mode).await {
                warn!("Failed to apply power mode {:?}: {:?}", power_mode, e);
                continue;
            }

            if Some(power_mode) != current_power_mode {
                self.telemetry_sender.send(telemetry::TelemetryEvent::UpdateExperiment {
                    experiment: telemetry::experiment::ExperimentUpdate::Power(power_mode),
                });
                current_power_mode = Some(power_mode);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            mode_management::{
                phy_manager::{CreateClientIfacesReason, PhyManagerError},
                Defect,
            },
            regulatory_manager::REGION_CODE_LEN,
        },
        anyhow::format_err,
        async_trait::async_trait,
        eui48::MacAddress,
        fuchsia_async, fuchsia_zircon as zx,
        futures::{channel::mpsc, task::Poll, StreamExt},
        pin_utils::pin_mut,
        std::unimplemented,
        test_case::test_case,
        wlan_common::assert_variant,
    };

    #[test_case(0, fidl_common::PowerSaveType::PsModeUltraLowPower; "ps mode ultra low")]
    #[test_case(1, fidl_common::PowerSaveType::PsModeLowPower; "ps mode low")]
    #[test_case(2, fidl_common::PowerSaveType::PsModeBalanced; "ps mode medium")]
    #[test_case(3, fidl_common::PowerSaveType::PsModePerformance; "ps mode high")]
    #[test_case(9999, fidl_common::PowerSaveType::PsModePerformance; "ps mode higher than expected")]
    fn test_power_level_conversion(level: u64, expected_mode: fidl_common::PowerSaveType) {
        assert_eq!(power_level_to_mode(level), expected_mode)
    }

    struct TestValues {
        phy_manager: FakePhyManager,
        telemetry_sender: telemetry::TelemetrySender,
        telemetry_receiver: mpsc::Receiver<telemetry::TelemetryEvent>,
        watcher_proxy: fidl_power::WatcherProxy,
        watcher_svc: fidl_power::WatcherRequestStream,
    }

    fn test_setup() -> TestValues {
        let (watcher_proxy, watcher_svc) =
            fidl::endpoints::create_proxy::<fidl_power::WatcherMarker>()
                .expect("failed to create watcher.");
        let watcher_svc = watcher_svc.into_stream().expect("failed to create watcher stream.");
        let phy_manager = FakePhyManager::new();
        let (mpsc_sender, telemetry_receiver) = mpsc::channel(10);
        let telemetry_sender = telemetry::TelemetrySender::new(mpsc_sender);

        TestValues { phy_manager, telemetry_sender, telemetry_receiver, watcher_proxy, watcher_svc }
    }

    #[test]
    fn test_no_wlan_power_config() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_vals = test_setup();

        // Drop the watcher service end so that watcher update requests fail.
        drop(test_vals.watcher_svc);

        // Create a PowerModeManager and run it.
        let lpm = PowerModeManager::new(
            test_vals.watcher_proxy,
            Arc::new(Mutex::new(test_vals.phy_manager)),
            test_vals.telemetry_sender,
        );
        let fut = lpm.run();
        pin_mut!(fut);

        // The future should exit immediately with an error since the low power state could not
        // be queried.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()))
    }

    #[test]
    fn test_power_watcher_drops() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_vals = test_setup();

        // Create a PowerModeManager and run it.
        let lpm = PowerModeManager::new(
            test_vals.watcher_proxy,
            Arc::new(Mutex::new(test_vals.phy_manager)),
            test_vals.telemetry_sender,
        );
        let fut = lpm.run();
        pin_mut!(fut);

        // The future should stall waiting for an update.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Read the watcher request and reply.
        assert_variant!(
            exec.run_until_stalled(&mut test_vals.watcher_svc.next()),
            Poll::Ready(Some(Ok(fidl_power::WatcherRequest::Watch { responder }))) => {
                responder
                    .send(4_u64)
                    .expect("failed to send power state");
            }
        );

        // Drop the watcher service end so that the subsequent watcher update requests fail.
        drop(test_vals.watcher_svc);

        // The future should exit since this is presumed to be a platform for which WLAN power
        // updates are available.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));

        // Make sure the update to performance mode came through.
        assert_variant!(
            exec.run_until_stalled(&mut test_vals.telemetry_receiver.next()),
            Poll::Ready(Some(telemetry::TelemetryEvent::UpdateExperiment {
                experiment: telemetry::experiment::ExperimentUpdate::Power(
                    fidl_common::PowerSaveType::PsModePerformance,
                )
            }))
        );
    }

    #[test]
    fn test_applying_power_setting_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_vals = test_setup();

        // Configure the FakePhyManager so that setting the power state fails.
        test_vals.phy_manager.set_power_succeeds = false;
        let phy_manager = Arc::new(Mutex::new(test_vals.phy_manager));
        let desired_power_state = fidl_common::PowerSaveType::PsModePerformance;

        // Create a PowerModeManager and run it.
        let lpm = PowerModeManager::new(
            test_vals.watcher_proxy,
            phy_manager.clone(),
            test_vals.telemetry_sender,
        );
        let fut = lpm.run();
        pin_mut!(fut);

        // The future should stall waiting for an update.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Read the watcher request and reply.
        assert_variant!(
            exec.run_until_stalled(&mut test_vals.watcher_svc.next()),
            Poll::Ready(Some(Ok(fidl_power::WatcherRequest::Watch { responder }))) => {
                responder
                    .send(desired_power_state as u64)
                    .expect("failed to send power state");
            }
        );

        // The future should process the reply, fail to apply the change, and continue running.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // There should not be a power state update since setting the power state failed.
        assert_variant!(
            exec.run_until_stalled(&mut test_vals.telemetry_receiver.next()),
            Poll::Pending
        );
    }

    #[test_case(fidl_common::PowerSaveType::PsModePerformance)]
    #[test_case(fidl_common::PowerSaveType::PsModeBalanced)]
    #[test_case(fidl_common::PowerSaveType::PsModeLowPower)]
    #[test_case(fidl_common::PowerSaveType::PsModeUltraLowPower)]
    fn test_applying_power_setting_succeeds(desired_power_state: fidl_common::PowerSaveType) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_vals = test_setup();
        let phy_manager = Arc::new(Mutex::new(test_vals.phy_manager));

        // Create a PowerModeManager and run it.
        let lpm = PowerModeManager::new(
            test_vals.watcher_proxy,
            phy_manager.clone(),
            test_vals.telemetry_sender,
        );
        let fut = lpm.run();
        pin_mut!(fut);

        // The future should stall waiting for an update.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Read the watcher request and reply.
        assert_variant!(
            exec.run_until_stalled(&mut test_vals.watcher_svc.next()),
            Poll::Ready(Some(Ok(fidl_power::WatcherRequest::Watch { responder }))) => {
                responder
                    .send(desired_power_state as u64)
                    .expect("failed to send power state");
            }
        );

        // The future should process the reply, apply the change, and then wait for the next
        // update.
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        let phy_manager_fut = phy_manager.lock();
        pin_mut!(phy_manager_fut);
        assert_variant!(
            exec.run_until_stalled(&mut phy_manager_fut),
            Poll::Ready(phy_manager) => {
                assert_eq!(phy_manager.power_state, Some(desired_power_state))
            }
        );

        // There should be a power state update indicating that the power mode has been changed.
        assert_variant!(
            exec.run_until_stalled(&mut test_vals.telemetry_receiver.next()),
            Poll::Ready(Some(telemetry::TelemetryEvent::UpdateExperiment { experiment })) => {
                assert_eq!(
                    telemetry::experiment::ExperimentUpdate::Power(desired_power_state),
                    experiment
                );
            }
        );
    }

    #[derive(Debug)]
    struct FakePhyManager {
        power_state: Option<fidl_common::PowerSaveType>,
        set_power_succeeds: bool,
    }

    impl FakePhyManager {
        fn new() -> Self {
            FakePhyManager { power_state: None, set_power_succeeds: true }
        }
    }

    #[async_trait]
    impl PhyManagerApi for FakePhyManager {
        async fn add_phy(&mut self, _phy_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn remove_phy(&mut self, _phy_id: u16) {
            unimplemented!();
        }

        async fn on_iface_added(&mut self, _iface_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn on_iface_removed(&mut self, _iface_id: u16) {
            unimplemented!();
        }

        async fn create_all_client_ifaces(
            &mut self,
            _reason: CreateClientIfacesReason,
        ) -> Result<Vec<u16>, (Vec<u16>, PhyManagerError)> {
            unimplemented!();
        }

        fn client_connections_enabled(&self) -> bool {
            unimplemented!();
        }

        async fn destroy_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn get_client(&mut self) -> Option<u16> {
            unimplemented!();
        }

        fn get_wpa3_capable_client(&mut self) -> Option<u16> {
            unimplemented!();
        }

        async fn create_or_get_ap_iface(&mut self) -> Result<Option<u16>, PhyManagerError> {
            unimplemented!();
        }

        async fn destroy_ap_iface(&mut self, _iface_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        async fn destroy_all_ap_ifaces(&mut self) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn suggest_ap_mac(&mut self, _mac: MacAddress) {
            unimplemented!();
        }

        fn get_phy_ids(&self) -> Vec<u16> {
            unimplemented!();
        }

        fn log_phy_add_failure(&mut self) {
            unimplemented!();
        }

        async fn set_country_code(
            &mut self,
            _country_code: Option<[u8; REGION_CODE_LEN]>,
        ) -> Result<(), PhyManagerError> {
            unimplemented!();
        }

        fn has_wpa3_client_iface(&self) -> bool {
            unimplemented!();
        }

        async fn set_power_state(
            &mut self,
            power_state: fidl_common::PowerSaveType,
        ) -> Result<fuchsia_zircon::Status, anyhow::Error> {
            if self.set_power_succeeds {
                self.power_state = Some(power_state);
                Ok(zx::Status::OK)
            } else {
                Err(format_err!("failed to set power state"))
            }
        }

        async fn record_defect(&mut self, _defect: Defect) {
            unimplemented!();
        }
    }
}
