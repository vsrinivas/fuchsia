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
