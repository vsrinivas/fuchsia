// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use futures::prelude::*;
use lowpan_driver_common::net::*;
use lowpan_driver_common::spinel::mock::*;
use lowpan_driver_common::spinel::*;

use fidl_fuchsia_lowpan::{
    ConnectivityState, Credential, Identity, ProvisioningParams, NET_TYPE_THREAD_1_X,
};

use lowpan_driver_common::Driver as _;
use openthread_fuchsia::Platform;

use std::str::FromStr;

#[fasync::run(10, test)]
async fn test_lowpan_ot_driver() {
    fuchsia_syslog::LOGGER.set_severity(fuchsia_syslog::levels::TRACE);

    let (sink, stream, ncp_task) = new_fake_spinel_pair();
    let ncp_task = fasync::Task::spawn(ncp_task);
    let instance = ot::Instance::new(Platform::init(sink, stream));

    let network_interface = DummyNetworkInterface::default();
    let driver = OtDriver::new(instance, network_interface);
    let driver_stream = driver.main_loop_stream();

    let app_task = async {
        let mut device_state_stream = driver.watch_device_state();
        let mut identity_stream = driver.watch_identity();

        fx_log_debug!("app_task: Checking device state... (Should be Inactive)");
        assert_eq!(
            device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
            ConnectivityState::Inactive
        );

        fx_log_debug!("app_task: Making sure it only vends one state when nothing has changed.");
        assert!(device_state_stream.next().now_or_never().is_none());

        for i in 1u8..32 {
            fx_log_debug!("app_task: Iteration {}", i);

            let channels = driver.get_supported_channels().await;
            fx_log_debug!("app_task: Supported channels: {:?}", channels);
            assert_eq!(channels.map(|_| ()), Ok(()));

            let fact_mac = driver.get_factory_mac_address().await;
            fx_log_debug!("app_task: Factory MAC: {:?}", fact_mac);
            assert_eq!(fact_mac.map(|_| ()), Ok(()));

            let curr_mac = driver.get_current_mac_address().await;
            fx_log_debug!("app_task: Current MAC: {:?}", curr_mac);
            assert_eq!(curr_mac.map(|_| ()), Ok(()));

            let ncp_ver = driver.get_ncp_version().await;
            fx_log_debug!("app_task: NCP Version: {:?}", ncp_ver);
            assert_eq!(ncp_ver.map(|_| ()), Ok(()));

            let network_types = driver.get_supported_network_types().await;
            fx_log_debug!("app_task: Supported Network Types: {:?}", network_types);
            assert_eq!(
                network_types,
                Ok(vec![fidl_fuchsia_lowpan::NET_TYPE_THREAD_1_X.to_string()])
            );

            let curr_chan = driver.get_current_channel().await;
            fx_log_debug!("app_task: Current Channel: {:?}", curr_chan);
            assert_eq!(curr_chan.map(|_| ()), Ok(()));

            let curr_rssi = driver.get_current_rssi().await;
            fx_log_debug!("app_task: Current RSSI: {:?}", curr_rssi);
            assert_eq!(curr_rssi.map(|_| ()), Ok(()));

            let part_id = driver.get_partition_id().await;
            fx_log_debug!("app_task: partition id: {:?}", part_id);
            assert_eq!(part_id.map(|_| ()), Ok(()));

            let thread_rloc16 = driver.get_thread_rloc16().await;
            fx_log_debug!("app_task: thread_rloc16: {:?}", thread_rloc16);
            assert_eq!(thread_rloc16.map(|_| ()), Ok(()));

            let thread_neighbor_table = driver.get_neighbor_table().await;
            fx_log_debug!("app_task: thread_neighbor_table: {:?}", thread_neighbor_table);
            let _thread_neighbor_entry_vec = thread_neighbor_table.unwrap();

            fx_log_debug!("app_task: Attempting a reset...");
            assert_eq!(driver.reset().await, Ok(()));
            fx_log_debug!("app_task: Did reset!");

            fx_log_debug!("app_task: Checking device state... (Should be Inactive) (1)");
            assert_eq!(
                driver
                    .watch_device_state()
                    .try_next()
                    .await
                    .expect("Error in device state stream")
                    .expect("Device state stream ended unexpectedly")
                    .connectivity_state
                    .expect("Connectivity state missing from device state"),
                ConnectivityState::Inactive
            );

            fx_log_debug!("app_task: Setting identity...");
            let test_identity = Identity {
                raw_name: Some("MyNetwork".as_bytes().to_vec()),
                xpanid: Some([0, 1, 2, 3, 4, 5, 6, 7].to_vec()),
                net_type: Some(NET_TYPE_THREAD_1_X.to_string()),
                channel: Some(22),
                panid: Some(0x1234),
                ..Identity::EMPTY
            };
            let test_credential =
                Credential::MasterKey(vec![0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
            assert_eq!(
                driver
                    .provision_network(ProvisioningParams {
                        identity: test_identity.clone(),
                        credential: Some(Box::new(test_credential.clone())),
                    })
                    .await,
                Ok(())
            );
            fx_log_debug!("app_task: Did provision!");

            fx_log_debug!("app_task: Checking device state... (Should be Ready)");
            assert_eq!(
                driver
                    .watch_device_state()
                    .try_next()
                    .await
                    .expect("Error in device state stream")
                    .expect("Device state stream ended unexpectedly")
                    .connectivity_state
                    .expect("Connectivity state missing from device state"),
                ConnectivityState::Ready
            );

            fx_log_debug!("app_task: Checking identity...");
            let new_identity = identity_stream
                .try_next()
                .await
                .expect("Error in identity stream")
                .expect("Identity stream ended unexpectedly");
            assert_eq!(
                new_identity.raw_name.map(ot::NetworkName::try_from),
                test_identity.raw_name.map(ot::NetworkName::try_from)
            );
            assert_eq!(new_identity.xpanid, test_identity.xpanid);
            assert_eq!(new_identity.panid, test_identity.panid);
            assert_eq!(new_identity.channel, test_identity.channel);
            assert_eq!(new_identity.net_type, test_identity.net_type);
            fx_log_debug!("app_task: Identity is correct!");

            fx_log_debug!("app_task: Checking credential...");
            assert_eq!(driver.get_credential().await, Ok(Some(test_credential)));
            fx_log_debug!("app_task: Credential is correct!");

            fx_log_debug!("app_task: Setting enabled...");
            assert_eq!(driver.set_active(true).await, Ok(()));
            fx_log_debug!("app_task: Did enable!");

            fx_log_debug!("app_task: Checking on-mesh prefixes to make sure it is empty...");
            assert_eq!(driver.get_local_on_mesh_prefixes().await, Ok(vec![]));
            fx_log_debug!("app_task: Is empty!");

            fx_log_debug!("app_task: Adding an on-mesh prefix...");
            let on_mesh_prefix_subnet: fidl_fuchsia_lowpan::Ipv6Subnet = Subnet {
                addr: std::net::Ipv6Addr::from_str("2001:DB8::").unwrap(),
                prefix_len: 64,
            }
            .into();
            let x = fidl_fuchsia_lowpan_device::OnMeshPrefix {
                subnet: Some(on_mesh_prefix_subnet.clone()),
                slaac_preferred: Some(true),
                slaac_valid: Some(true),
                ..fidl_fuchsia_lowpan_device::OnMeshPrefix::EMPTY
            };
            assert_eq!(driver.register_on_mesh_prefix(x.clone()).await, Ok(()));
            fx_log_debug!("app_task: Registered!");

            fuchsia_async::Timer::new(fuchsia_async::Duration::from_millis(50)).await;

            fx_log_debug!("app_task: Checking on-mesh prefixes...");
            assert_eq!(
                driver
                    .get_local_on_mesh_prefixes()
                    .map(|x| x.context("get_local_on_mesh_prefixes"))
                    .and_then(|x| futures::future::ready(
                        x.first()
                            .ok_or(format_err!("on-mesh-prefixes is empty"))
                            .map(|x| x.subnet.clone())
                    ))
                    .await
                    .unwrap(),
                x.subnet
            );
            fx_log_debug!("app_task: Populated!");

            fx_log_debug!("app_task: Removing an on-mesh prefix...");
            assert_eq!(
                driver.unregister_on_mesh_prefix(on_mesh_prefix_subnet.clone()).await,
                Ok(())
            );
            fx_log_debug!("app_task: Unregistered!");

            fx_log_debug!("app_task: Checking on-mesh prefixes to make sure it is empty...");
            assert_eq!(driver.get_local_on_mesh_prefixes().await, Ok(vec![]));
            fx_log_debug!("app_task: Is empty!");

            fx_log_debug!("app_task: Checking device state... (Should be Attaching)");
            assert_eq!(
                device_state_stream
                    .try_next()
                    .await
                    .expect("Error in device state stream")
                    .expect("Device state stream ended unexpectedly")
                    .connectivity_state
                    .unwrap(),
                ConnectivityState::Attaching
            );

            fx_log_debug!("app_task: Setting disabled...");
            assert_eq!(driver.set_active(false).await, Ok(()));
            fx_log_debug!("app_task: Did disable!");

            fx_log_debug!("app_task: Checking device state... (Should be Ready)");
            assert_eq!(
                device_state_stream
                    .try_next()
                    .await
                    .expect("Error in device state stream")
                    .expect("Device state stream ended unexpectedly")
                    .connectivity_state
                    .unwrap(),
                ConnectivityState::Ready
            );

            fx_log_debug!("app_task: Leaving network...");
            assert_eq!(driver.leave_network().await, Ok(()));
            fx_log_debug!("app_task: Did leave!");

            fx_log_debug!("app_task: Checking device state... (Should be Inactive)");
            assert_eq!(
                device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
                ConnectivityState::Inactive
            );

            fx_log_debug!("app_task: Setting enabled...");
            assert_eq!(driver.set_active(true).await, Ok(()));
            fx_log_debug!("app_task: Did enable!");

            fx_log_debug!("app_task: Checking device state... (Should be Offline)");
            assert_eq!(
                device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
                ConnectivityState::Offline
            );

            // TODO: Re-enable once scan support is added to test RCP
            // fx_log_debug!("app_task: Performing network scan...");
            // let network_scan_stream = driver
            //     .start_network_scan(&fidl_fuchsia_lowpan_device::NetworkScanParameters::EMPTY);
            // assert_eq!(network_scan_stream.try_collect::<Vec<_>>().await.unwrap().len(), 3);

            // TODO: Re-enable once scan support is added to test RCP
            // fx_log_debug!("app_task: Performing energy scan...");
            // let energy_scan_stream =
            //     driver.start_energy_scan(&fidl_fuchsia_lowpan_device::EnergyScanParameters::EMPTY);
            // assert_eq!(energy_scan_stream.try_collect::<Vec<_>>().await.unwrap().len(), 3);

            fx_log_debug!("app_task: Setting disabled...");
            assert_eq!(driver.set_active(false).await, Ok(()));
            fx_log_debug!("app_task: Did disable!");

            fx_log_debug!("app_task: Checking device state... (Should be Inactive) (2)");
            assert_eq!(
                device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
                ConnectivityState::Inactive
            );
        }
    };

    let mut driver_stream_count = 0u32;

    futures::select_biased! {
        ret = driver_stream.try_for_each(move |_| {
            driver_stream_count += 1;
            assert!(driver_stream_count < 1000, "STUCK: Driver stream had 10^3 iterations");
            futures::future::ready(Ok(()))
        }).fuse()
            => panic!("Driver stream error: {:?}", ret),
        ret = ncp_task.fuse()
            => panic!("NCP task error: {:?}", ret),
        _ = app_task.boxed().fuse() => (),
    }
}
