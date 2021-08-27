// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use crate::prelude::*;
use crate::spinel::*;
use futures::prelude::*;
use mock::*;

use crate::spinel::mock::{PROP_DEBUG_LOGGING_TEST, PROP_DEBUG_SAVED_PANID_TEST};
use fidl_fuchsia_lowpan::{Credential, Identity, ProvisioningParams, NET_TYPE_THREAD_1_X};
use fidl_fuchsia_lowpan_device::{AllCounters, MacCounters};
use fidl_fuchsia_lowpan_test::NeighborInfo;
use lowpan_driver_common::Driver as _;
use std::str::FromStr;

impl<DS, NI> SpinelDriver<DS, NI> {
    pub(super) fn get_driver_state_snapshot(&self) -> DriverState {
        self.driver_state.lock().clone()
    }
}

// TODO(fxbug.dev/81732): go back to #[fasync::run_until_stalled(test)] at some point.
#[fasync::run_singlethreaded(test)]
async fn test_spinel_lowpan_driver() {
    let (device_client, device_stream, ncp_task) = new_fake_spinel_pair();
    let network_interface = DummyNetworkInterface::default();
    let driver = SpinelDriver::new(device_client, network_interface);
    let driver_stream = driver.wrap_inbound_stream(device_stream);

    assert_eq!(driver.get_driver_state_snapshot().caps.len(), 0);

    let app_task = async {
        // Wait until we are ready.
        driver.wait_for_state(DriverState::is_initialized).await;

        // Verify that our capabilities have been set by this point.
        assert_eq!(driver.get_driver_state_snapshot().caps.len(), 3);

        let mut device_state_stream = driver.watch_device_state();
        let mut identity_stream = driver.watch_identity();

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

            let thread_neighbor_table = driver.get_neighbor_table().await;
            traceln!("app_task: thread_neighbor_table: {:?}", thread_neighbor_table);
            let thread_neighbor_entry_vec = thread_neighbor_table.unwrap();
            assert_eq!(
                thread_neighbor_entry_vec[0],
                NeighborInfo {
                    mac_address: Some([0, 1, 2, 3, 4, 5, 6, 7].to_vec()),
                    short_address: Some(0x123),
                    age: Some(fuchsia_zircon::Duration::from_seconds(11).into_nanos()),
                    is_child: Some(true),
                    link_frame_count: Some(1),
                    mgmt_frame_count: Some(1),
                    last_rssi_in: Some(-20),
                    avg_rssi_in: Some(-20),
                    lqi_in: Some(3),
                    thread_mode: Some(0x0b),
                    ..NeighborInfo::EMPTY
                }
            );
            assert_eq!(
                thread_neighbor_entry_vec[1],
                NeighborInfo {
                    mac_address: Some([1, 2, 3, 4, 5, 6, 7, 8].to_vec()),
                    short_address: Some(0x1234),
                    age: Some(fuchsia_zircon::Duration::from_seconds(22).into_nanos()),
                    is_child: Some(true),
                    link_frame_count: Some(1),
                    mgmt_frame_count: Some(1),
                    last_rssi_in: Some(-30),
                    avg_rssi_in: Some(-30),
                    lqi_in: Some(4),
                    thread_mode: Some(0x0b),
                    ..NeighborInfo::EMPTY
                }
            );

            let mac_counters = driver.get_counters().await;
            traceln!("app_task: mac_counters: {:?}", mac_counters);
            assert_eq!(
                mac_counters,
                Ok(AllCounters {
                    mac_tx: Some(MacCounters {
                        total: Some(0),
                        unicast: Some(1),
                        broadcast: Some(2),
                        ack_requested: Some(3),
                        acked: Some(4),
                        no_ack_requested: Some(5),
                        data: Some(6),
                        data_poll: Some(7),
                        beacon: Some(8),
                        beacon_request: Some(9),
                        other: Some(10),
                        address_filtered: None,
                        retries: Some(11),
                        direct_max_retry_expiry: Some(15),
                        indirect_max_retry_expiry: Some(16),
                        dest_addr_filtered: None,
                        duplicated: None,
                        err_no_frame: None,
                        err_unknown_neighbor: None,
                        err_invalid_src_addr: None,
                        err_sec: None,
                        err_fcs: None,
                        err_cca: Some(12),
                        err_abort: Some(13),
                        err_busy_channel: Some(14),
                        err_other: None,
                        ..MacCounters::EMPTY
                    }),
                    mac_rx: Some(MacCounters {
                        total: Some(100),
                        unicast: Some(101),
                        broadcast: Some(102),
                        ack_requested: None,
                        acked: None,
                        no_ack_requested: None,
                        data: Some(103),
                        data_poll: Some(104),
                        beacon: Some(105),
                        beacon_request: Some(106),
                        other: Some(107),
                        address_filtered: Some(108),
                        retries: None,
                        direct_max_retry_expiry: None,
                        indirect_max_retry_expiry: None,
                        dest_addr_filtered: Some(109),
                        duplicated: Some(110),
                        err_no_frame: Some(111),
                        err_unknown_neighbor: Some(112),
                        err_invalid_src_addr: Some(113),
                        err_sec: Some(114),
                        err_fcs: Some(115),
                        err_cca: None,
                        err_abort: None,
                        err_busy_channel: None,
                        err_other: Some(116),
                        ..MacCounters::EMPTY
                    }),
                    ..AllCounters::EMPTY
                })
            );

            let mac_counters = driver.reset_counters().await;
            traceln!("app_task: reset_counters: {:?}", mac_counters);
            assert_eq!(
                mac_counters,
                Ok(AllCounters {
                    mac_tx: Some(MacCounters {
                        total: Some(0),
                        unicast: Some(1),
                        broadcast: Some(2),
                        ack_requested: Some(3),
                        acked: Some(4),
                        no_ack_requested: Some(5),
                        data: Some(6),
                        data_poll: Some(7),
                        beacon: Some(8),
                        beacon_request: Some(9),
                        other: Some(10),
                        retries: Some(11),
                        direct_max_retry_expiry: Some(15),
                        indirect_max_retry_expiry: Some(16),
                        err_cca: Some(12),
                        err_abort: Some(13),
                        err_busy_channel: Some(14),
                        ..MacCounters::EMPTY
                    }),
                    mac_rx: Some(MacCounters {
                        total: Some(100),
                        unicast: Some(101),
                        broadcast: Some(102),
                        data: Some(103),
                        data_poll: Some(104),
                        beacon: Some(105),
                        beacon_request: Some(106),
                        other: Some(107),
                        address_filtered: Some(108),
                        dest_addr_filtered: Some(109),
                        duplicated: Some(110),
                        err_no_frame: Some(111),
                        err_unknown_neighbor: Some(112),
                        err_invalid_src_addr: Some(113),
                        err_sec: Some(114),
                        err_fcs: Some(115),
                        err_other: Some(116),
                        ..MacCounters::EMPTY
                    }),
                    ..AllCounters::EMPTY
                })
            );

            traceln!("app_task: Attempting a reset...");
            assert_eq!(driver.reset().await, Ok(()));
            traceln!("app_task: Did reset!");

            traceln!("app_task: Checking device state... (Should be Inactive) (1)");
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

            traceln!("app_task: Setting identity...");
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
            traceln!("app_task: Did provision!");

            traceln!("app_task: Checking device state... (Should be Ready)");
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

            traceln!("app_task: Checking identity...");
            let new_identity = identity_stream
                .try_next()
                .await
                .expect("Error in identity stream")
                .expect("Identity stream ended unexpectedly");
            assert_eq!(new_identity.raw_name, test_identity.raw_name);
            assert_eq!(new_identity.xpanid, test_identity.xpanid);
            assert_eq!(new_identity.panid, test_identity.panid);
            assert_eq!(new_identity.channel, test_identity.channel);
            assert_eq!(new_identity.net_type, test_identity.net_type);
            traceln!("app_task: Identity is correct!");

            traceln!("app_task: Checking credential...");
            assert_eq!(driver.get_credential().await, Ok(Some(test_credential)));
            traceln!("app_task: Credential is correct!");

            traceln!("app_task: Setting enabled...");
            assert_eq!(driver.set_active(true).await, Ok(()));
            traceln!("app_task: Did enable!");

            traceln!("app_task: Checking on-mesh prefixes to make sure it is empty...");
            assert_eq!(driver.get_local_on_mesh_prefixes().await, Ok(vec![]));
            traceln!("app_task: Is empty!");

            traceln!("app_task: Adding an on-mesh prefix...");
            let on_mesh_prefix_subnet: fidl_fuchsia_lowpan::Ipv6Subnet = Subnet {
                addr: std::net::Ipv6Addr::from_str("fd00:abcd:1234::").unwrap(),
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
            traceln!("app_task: Registered!");

            traceln!("app_task: Checking on-mesh prefixes...");
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
            traceln!("app_task: Populated!");

            traceln!("app_task: Removing an on-mesh prefix...");
            assert_eq!(
                driver.unregister_on_mesh_prefix(on_mesh_prefix_subnet.clone()).await,
                Ok(())
            );
            traceln!("app_task: Unregistered!");

            traceln!("app_task: Checking on-mesh prefixes to make sure it is empty...");
            assert_eq!(driver.get_local_on_mesh_prefixes().await, Ok(vec![]));
            traceln!("app_task: Is empty!");

            traceln!("app_task: Checking device state... (Should be Attaching)");
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

            traceln!("app_task: Setting disabled...");
            assert_eq!(driver.set_active(false).await, Ok(()));
            traceln!("app_task: Did disable!");

            traceln!("app_task: Checking device state... (Should be Ready)");
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

            traceln!("app_task: Changing the saved PANID...");
            driver
                .frame_handler
                .send_request(CmdPropValueSet(PROP_DEBUG_SAVED_PANID_TEST, 1337u16))
                .await
                .expect("Unable to set PROP_DEBUG_SAVED_PANID_TEST");

            traceln!("app_task: Resetting device...");
            assert_eq!(driver.reset().await, Ok(()));
            traceln!("app_task: Did reset!");

            traceln!("app_task: Checking identity...");
            let new_identity = identity_stream
                .try_next()
                .await
                .expect("Error in identity stream")
                .expect("Identity stream ended unexpectedly");
            traceln!("app_task: Identity: {:?}", &new_identity);
            assert_eq!(new_identity.panid, Some(1337u16));
            assert_eq!(new_identity.raw_name, test_identity.raw_name);
            assert_eq!(new_identity.xpanid, test_identity.xpanid);
            assert_eq!(new_identity.channel, test_identity.channel);
            assert_eq!(new_identity.net_type, test_identity.net_type);
            traceln!("app_task: Identity is correct!");

            traceln!("app_task: Leaving network...");
            assert_eq!(driver.leave_network().await, Ok(()));
            traceln!("app_task: Did leave!");

            traceln!("app_task: Checking device state... (Should be Inactive)");
            assert_eq!(
                device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
                ConnectivityState::Inactive
            );

            traceln!("app_task: Setting enabled...");
            assert_eq!(driver.set_active(true).await, Ok(()));
            traceln!("app_task: Did enable!");

            traceln!("app_task: Checking device state... (Should be Offline)");
            assert_eq!(
                device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
                ConnectivityState::Offline
            );

            traceln!("app_task: Performing energy scan...");
            let energy_scan_stream =
                driver.start_energy_scan(&fidl_fuchsia_lowpan_device::EnergyScanParameters::EMPTY);
            assert_eq!(energy_scan_stream.try_collect::<Vec<_>>().await.unwrap().len(), 3);

            traceln!("app_task: Performing network scan...");
            let network_scan_stream = driver
                .start_network_scan(&fidl_fuchsia_lowpan_device::NetworkScanParameters::EMPTY);
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

            traceln!("app_task: Checking device state... (Should be Inactive) (2)");
            assert_eq!(
                device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
                ConnectivityState::Inactive
            );
        }
    };

    futures::select_biased! {
        ret = driver_stream.try_for_each(|_|futures::future::ready(Ok(()))).fuse()
            => panic!("Driver stream error: {:?}", ret),
        ret = ncp_task.fuse()
            => panic!("NCP task error: {:?}", ret),
        _ = app_task.boxed_local().fuse() => (),
    }
}
