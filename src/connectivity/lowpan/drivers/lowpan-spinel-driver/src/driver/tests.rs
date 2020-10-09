// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::prelude::*;
use crate::spinel::*;
use futures::prelude::*;
use mock::*;

use crate::spinel::mock::PROP_DEBUG_LOGGING_TEST;
use fidl_fuchsia_lowpan::{Credential, Identity, ProvisioningParams, NET_TYPE_THREAD_1_X};
use lowpan_driver_common::Driver as _;

impl<DS> SpinelDriver<DS> {
    pub(super) fn get_driver_state_snapshot(&self) -> DriverState {
        self.driver_state.lock().clone()
    }
}

#[fasync::run_until_stalled(test)]
async fn test_spinel_lowpan_driver() {
    let (device_client, device_stream, ncp_task) = new_fake_spinel_pair();

    let driver = SpinelDriver::from(device_client);
    let driver_stream = driver.wrap_inbound_stream(device_stream);

    assert_eq!(driver.get_driver_state_snapshot().caps.len(), 0);

    let app_task = async {
        // Wait until we are ready.
        driver.wait_for_state(DriverState::is_initialized).await;

        // Verify that our capabilities have been set by this point.
        assert_eq!(driver.get_driver_state_snapshot().caps.len(), 2);

        let mut device_state_stream = driver.watch_device_state();

        traceln!("app_task: Checking device state... (Should be Inactive)");
        assert_eq!(
            device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
            ConnectivityState::Inactive
        );

        traceln!("app_task: Making sure it only vends one state when nothing has changed.");
        assert!(device_state_stream.next().now_or_never().is_none());

        for i in 1u8..32 {
            traceln!("app_task: Iteration {}", i);

            let channels = driver.get_supported_channels().await;
            traceln!("app_task: Supported channels: {:?}", channels);
            assert_eq!(channels.map(|_| ()), Ok(()));

            let fact_mac = driver.get_factory_mac_address().await;
            traceln!("app_task: Factory MAC: {:?}", fact_mac);
            assert_eq!(fact_mac.map(|_| ()), Ok(()));

            let curr_mac = driver.get_current_mac_address().await;
            traceln!("app_task: Current MAC: {:?}", curr_mac);
            assert_eq!(curr_mac.map(|_| ()), Ok(()));

            let ncp_ver = driver.get_ncp_version().await;
            traceln!("app_task: NCP Version: {:?}", ncp_ver);
            assert_eq!(ncp_ver.map(|_| ()), Ok(()));

            let network_types = driver.get_supported_network_types().await;
            traceln!("app_task: Supported Network Types: {:?}", network_types);
            assert_eq!(
                network_types,
                Ok(vec![fidl_fuchsia_lowpan::NET_TYPE_THREAD_1_X.to_string()])
            );

            let curr_chan = driver.get_current_channel().await;
            traceln!("app_task: Current Channel: {:?}", curr_chan);
            assert_eq!(curr_chan.map(|_| ()), Ok(()));

            let curr_rssi = driver.get_current_rssi().await;
            traceln!("app_task: Current RSSI: {:?}", curr_rssi);
            assert_eq!(curr_rssi.map(|_| ()), Ok(()));

            let part_id = driver.get_partition_id().await;
            traceln!("app_task: partition id: {:?}", part_id);
            assert_eq!(part_id.map(|_| ()), Ok(()));

            let thread_rloc16 = driver.get_thread_rloc16().await;
            traceln!("app_task: thread_rloc16: {:?}", thread_rloc16);
            assert_eq!(thread_rloc16.map(|_| ()), Ok(()));

            traceln!("app_task: Attempting a reset...");
            assert_eq!(driver.reset().await, Ok(()));
            traceln!("app_task: Did reset!");

            traceln!("app_task: Checking device state...  (Should be Inactive)");
            assert_eq!(
                driver
                    .watch_device_state()
                    .try_next()
                    .await
                    .unwrap()
                    .unwrap()
                    .connectivity_state
                    .unwrap(),
                ConnectivityState::Inactive
            );

            traceln!("app_task: Setting identity...");
            assert_eq!(
                driver
                    .provision_network(ProvisioningParams {
                        identity: Identity {
                            raw_name: Some("MyNetwork".as_bytes().to_vec()),
                            xpanid: Some([0, 1, 2, 3, 4, 5, 6, 7].to_vec()),
                            net_type: Some(NET_TYPE_THREAD_1_X.to_string()),
                            channel: Some(11),
                            panid: Some(0x1234),
                        },
                        credential: Some(Box::new(Credential::MasterKey(vec![
                            0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
                        ]))),
                    })
                    .await,
                Ok(())
            );
            traceln!("app_task: Did provision!");

            traceln!("app_task: Checking device state... (Should be Ready)");
            assert_eq!(
                driver
                    .watch_device_state()
                    .try_next()
                    .await
                    .unwrap()
                    .unwrap()
                    .connectivity_state
                    .unwrap(),
                ConnectivityState::Ready
            );

            traceln!("app_task: Checking credential...");
            assert_eq!(
                driver.get_credential().await,
                Ok(Some(Credential::MasterKey(vec![
                    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
                ])))
            );
            traceln!("app_task: Credential is correct!");

            traceln!("app_task: Leaving network...");
            assert_eq!(driver.leave_network().await, Ok(()));
            traceln!("app_task: Did leave!");

            traceln!("app_task: Setting enabled...");
            assert_eq!(driver.set_active(true).await, Ok(()));
            traceln!("app_task: Did enable!");

            traceln!("app_task: Checking device state...");
            assert_eq!(
                device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
                ConnectivityState::Offline
            );

            traceln!("app_task: Performing energy scan...");
            let energy_scan_stream = driver
                .start_energy_scan(&fidl_fuchsia_lowpan_device::EnergyScanParameters::empty());
            assert_eq!(energy_scan_stream.try_collect::<Vec<_>>().await.unwrap().len(), 3);

            traceln!("app_task: Performing network scan...");
            let network_scan_stream = driver
                .start_network_scan(&fidl_fuchsia_lowpan_device::NetworkScanParameters::empty());
            assert_eq!(network_scan_stream.try_collect::<Vec<_>>().await.unwrap().len(), 3);

            traceln!("app_task: Testing debug logging...");
            driver
                .frame_handler
                .send_request(CmdPropValueSet(PROP_DEBUG_LOGGING_TEST, ()))
                .await
                .unwrap();

            traceln!("app_task: Setting disabled...");
            assert_eq!(driver.set_active(false).await, Ok(()));
            traceln!("app_task: Did disable!");

            traceln!("app_task: Checking device state...");
            assert_eq!(
                device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
                ConnectivityState::Inactive
            );
        }
    };

    futures::select! {
        ret = driver_stream.try_for_each(|_|futures::future::ready(Ok(()))).fuse()
            => panic!("Driver stream error: {:?}", ret),
        ret = ncp_task.fuse()
            => panic!("NCP task error: {:?}", ret),
        _ = app_task.boxed_local().fuse() => (),
    }
}
