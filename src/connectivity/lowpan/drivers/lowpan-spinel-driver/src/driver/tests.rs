// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::prelude::*;
use crate::spinel::*;
use futures::prelude::*;
use mock::*;

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
