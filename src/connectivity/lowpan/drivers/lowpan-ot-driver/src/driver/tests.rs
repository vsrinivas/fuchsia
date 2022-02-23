// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

use fidl_fuchsia_lowpan::{
    ConnectivityState, Credential, Identity, ProvisioningParams, NET_TYPE_THREAD_1_X,
};
use futures::prelude::*;
use lowpan_driver_common::net::*;
use lowpan_driver_common::spinel::mock::*;
use lowpan_driver_common::spinel::*;
use lowpan_driver_common::Driver as _;
use openthread_fuchsia::Platform;
use std::str::FromStr;
use std::sync::Arc;
use std::sync::Mutex;

lazy_static::lazy_static! {
    static ref TEST_HARNESS_SINGLETON_LOCK: Mutex<()> = Mutex::new(());

    static ref TEST_IDENTITY: Identity = Identity {
            raw_name: Some("MyNetwork".as_bytes().to_vec()),
            xpanid: Some([0, 1, 2, 3, 4, 5, 6, 7].to_vec()),
            net_type: Some(NET_TYPE_THREAD_1_X.to_string()),
            channel: Some(13),
            panid: Some(0x1234),
            ..Identity::EMPTY
        };

    static ref TEST_CREDENTIAL: Credential =
            Credential::MasterKey(vec![0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
}

const DEFAULT_TEST_TIMEOUT: Duration = Duration::from_seconds(45);

/// This method is our test harness for all of our individual tests.
/// It sets up a fake radio and fake network interface and allows
/// the program flow to be tested in a fairly straightforward manner.
fn test_harness<'a, Fun, Fut>(fun: Fun) -> impl Future<Output = ()> + Send + 'a
where
    Fun: FnOnce(Arc<OtDriver<OtInstanceBox, DummyNetworkInterface>>) -> Fut,
    Fut: Future<Output = ()> + Send + 'a,
{
    // Since OpenThread is a singleton, we can't create more than once instance at a time.
    // This lock makes sure that we only run a single test at a time so we don't panic.
    let _singleton_lock =
        TEST_HARNESS_SINGLETON_LOCK.lock().expect("TEST_HARNESS_SINGLETON_LOCK is poisoned");

    // Adjust our logging so that we don't get inundated with useless logs.
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::WARN);

    let (sink, stream, ncp_task) = new_fake_spinel_pair();

    // The following task must ultimately run in the background on
    // a separate thread in order for the OpenThread Fuchsia platform
    // to work properly. When used with the multithreaded executor,
    // this `spawn` call does the trick.
    let ncp_task = fasync::Task::spawn(ncp_task);

    let network_interface = DummyNetworkInterface::default();

    // Queue a network event that indicates we are enabled.
    network_interface.set_enabled(true).now_or_never().unwrap().unwrap();

    let instance = ot::Instance::new(Platform::build().init(sink, stream));

    // Ignore all persistent storage and perform a factory reset.
    instance.erase_persistent_info().unwrap();
    instance.factory_reset();

    ot::set_logging_level(ot::LogLevel::Crit);

    let driver = Arc::new(OtDriver::new(instance, network_interface));

    // Note that we cannot move this into an async block because
    // `fun` itself doesn't implement `Send`.
    let app_task = fun(driver.clone());

    let driver_clone = driver.clone();
    let app_task = async move {
        // Wait for the driver to handle the call to `network_interface.set_enabled(true)`
        // before moving on.
        driver_clone
            .wait_for_state(DriverState::is_active)
            .on_timeout(fasync::Time::after(DEFAULT_TEST_TIMEOUT), || {
                panic!("Initialization timed out");
            })
            .await;
        app_task
            .on_timeout(fasync::Time::after(DEFAULT_TEST_TIMEOUT), || {
                panic!("App task timed out");
            })
            .await;
    };

    async move {
        let driver_stream = driver.main_loop_stream();

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

        // Turn off OpenThread logging so that we don't see
        // the useless errors that crop up when we shutdown.
        ot::set_logging_level(ot::LogLevel::None);
    }
}

/// This is just a simple test where we bring everything up and
/// immediately tear it down. If this fails there are fundamental problems.
#[fasync::run(10, test)]
async fn test_driver_setup_and_teardown() {
    test_harness(|_| async move {}).await;
}

#[fasync::run(10, test)]
async fn test_initial_device_state() {
    test_harness(|driver| async move {
        let mut device_state_stream = driver.watch_device_state();
        let mut identity_stream = driver.watch_identity();

        assert_eq!(
            device_state_stream
                .try_next()
                .await
                .expect("Error in device state stream")
                .expect("Device state stream ended unexpectedly")
                .connectivity_state
                .expect("Connectivity state missing from device state"),
            ConnectivityState::Offline
        );

        assert!(device_state_stream.next().now_or_never().is_none());

        let new_identity = identity_stream
            .try_next()
            .await
            .expect("Error in identity stream")
            .expect("Identity stream ended unexpectedly");
        assert_eq!(new_identity, Identity::EMPTY);

        assert!(identity_stream.next().now_or_never().is_none());
    })
    .await;
}

#[fasync::run(10, test)]
#[ignore] // TODO: Re-enable once scan support is added to test RCP
async fn test_network_scan() {
    test_harness(|driver| async move {
        let network_scan_stream =
            driver.start_network_scan(&fidl_fuchsia_lowpan_device::NetworkScanParameters::EMPTY);
        assert_eq!(network_scan_stream.try_collect::<Vec<_>>().await.unwrap().len(), 3);
    })
    .await;
}

#[fasync::run(10, test)]
#[ignore] // TODO: Re-enable once joiner support is added to test RCP
async fn test_joiner() {
    use fidl_fuchsia_lowpan::JoinParams;
    use fidl_fuchsia_lowpan::JoinerCommissioningParams;

    test_harness(|driver| async move {
        let join_stream =
            driver.join_network(JoinParams::JoinerParameter(JoinerCommissioningParams {
                pskd: Some("ABCDEFG".to_string()),
                ..JoinerCommissioningParams::EMPTY
            }));
        join_stream.try_collect::<Vec<_>>().await.unwrap();
    })
    .await;
}

#[fasync::run(10, test)]
#[ignore] // TODO: Re-enable once scan support is added to test RCP
async fn test_energy_scan() {
    test_harness(|driver| async move {
        let energy_scan_stream =
            driver.start_energy_scan(&fidl_fuchsia_lowpan_device::EnergyScanParameters::EMPTY);
        assert_eq!(energy_scan_stream.try_collect::<Vec<_>>().await.unwrap().len(), 3);
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_get_supported_channels() {
    test_harness(|driver| async move {
        let channels = driver.get_supported_channels().await;
        assert_eq!(channels.map(|_| ()), Ok(()));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_get_factory_mac_address() {
    test_harness(|driver| async move {
        let fact_mac = driver.get_factory_mac_address().await;
        assert_eq!(fact_mac.map(|_| ()), Ok(()));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_get_current_mac_address() {
    test_harness(|driver| async move {
        let fact_mac = driver.get_current_mac_address().await;
        assert_eq!(fact_mac.map(|_| ()), Ok(()));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_get_supported_network_types() {
    test_harness(|driver| async move {
        let network_types = driver.get_supported_network_types().await;
        assert_eq!(network_types, Ok(vec![fidl_fuchsia_lowpan::NET_TYPE_THREAD_1_X.to_string()]));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_get_current_channel() {
    test_harness(|driver| async move {
        let curr_chan = driver.get_current_channel().await;
        assert_eq!(curr_chan.map(|_| ()), Ok(()));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_get_partition_id() {
    test_harness(|driver| async move {
        let part_id = driver.get_partition_id().await;
        assert_eq!(part_id.map(|_| ()), Ok(()));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_get_current_rssi() {
    test_harness(|driver| async move {
        let curr_rssi = driver.get_current_rssi().await;
        assert_eq!(curr_rssi.map(|_| ()), Ok(()));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_get_thread_rloc16() {
    test_harness(|driver| async move {
        let thread_rloc16 = driver.get_thread_rloc16().await;
        assert_eq!(thread_rloc16.map(|_| ()), Ok(()));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_get_neighbor_table_offline() {
    test_harness(|driver| async move {
        let thread_neighbor_table = driver.get_neighbor_table().await;
        assert_eq!(thread_neighbor_table, Ok(vec![]));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_provision_network_offline() {
    test_harness(|driver| async move {
        let mut identity_stream = driver.watch_identity();

        assert_eq!(driver.set_active(false).await, Ok(()));

        assert_eq!(
            driver
                .provision_network(ProvisioningParams {
                    identity: TEST_IDENTITY.clone(),
                    credential: Some(Box::new(TEST_CREDENTIAL.clone())),
                })
                .await,
            Ok(())
        );

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

        let new_identity = identity_stream
            .try_next()
            .await
            .expect("Error in identity stream")
            .expect("Identity stream ended unexpectedly");
        assert_eq!(
            new_identity.raw_name.map(ot::NetworkName::try_from),
            TEST_IDENTITY.raw_name.clone().map(ot::NetworkName::try_from)
        );
        assert_eq!(new_identity.xpanid, TEST_IDENTITY.xpanid);
        assert_eq!(new_identity.panid, TEST_IDENTITY.panid);
        assert_eq!(new_identity.channel, TEST_IDENTITY.channel);
        assert_eq!(new_identity.net_type, TEST_IDENTITY.net_type);

        assert_eq!(driver.get_credential().await, Ok(Some(TEST_CREDENTIAL.clone())));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_provision_network_online() {
    test_harness(|driver| async move {
        let mut device_state_stream = driver.watch_device_state();
        let mut identity_stream = driver.watch_identity();

        assert_eq!(
            device_state_stream
                .try_next()
                .await
                .expect("Error in device state stream")
                .expect("Device state stream ended unexpectedly")
                .connectivity_state
                .expect("Connectivity state missing from device state"),
            ConnectivityState::Offline
        );

        assert_eq!(
            driver
                .provision_network(ProvisioningParams {
                    identity: TEST_IDENTITY.clone(),
                    credential: Some(Box::new(TEST_CREDENTIAL.clone())),
                })
                .await,
            Ok(())
        );

        info!("Waiting to become 'Attaching'");
        assert_eq!(
            device_state_stream
                .try_next()
                .await
                .expect("Error in device state stream")
                .expect("Device state stream ended unexpectedly")
                .connectivity_state
                .expect("Connectivity state missing from device state"),
            ConnectivityState::Attaching
        );

        info!("Waiting to become 'Attached'");
        assert_eq!(
            device_state_stream
                .try_next()
                .await
                .expect("Error in device state stream")
                .expect("Device state stream ended unexpectedly")
                .connectivity_state
                .expect("Connectivity state missing from device state"),
            ConnectivityState::Attached
        );

        let new_identity = identity_stream
            .try_next()
            .await
            .expect("Error in identity stream")
            .expect("Identity stream ended unexpectedly");
        assert_eq!(
            new_identity.raw_name.map(ot::NetworkName::try_from),
            TEST_IDENTITY.raw_name.clone().map(ot::NetworkName::try_from)
        );
        assert_eq!(new_identity.xpanid, TEST_IDENTITY.xpanid);
        assert_eq!(new_identity.panid, TEST_IDENTITY.panid);
        assert_eq!(new_identity.channel, TEST_IDENTITY.channel);
        assert_eq!(new_identity.net_type, TEST_IDENTITY.net_type);

        assert_eq!(driver.get_credential().await, Ok(Some(TEST_CREDENTIAL.clone())));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_add_on_mesh_prefix() {
    test_harness(|driver| async move {
        assert_eq!(
            driver
                .provision_network(ProvisioningParams {
                    identity: TEST_IDENTITY.clone(),
                    credential: Some(Box::new(TEST_CREDENTIAL.clone())),
                })
                .await,
            Ok(())
        );

        assert_eq!(driver.set_active(true).await, Ok(()));

        driver.wait_for_state(DriverState::is_active_and_ready).await;

        let on_mesh_prefix_subnet: fidl_fuchsia_lowpan::Ipv6Subnet =
            Subnet { addr: std::net::Ipv6Addr::from_str("2001:DB8::").unwrap(), prefix_len: 64 }
                .into();
        let x = fidl_fuchsia_lowpan_device::OnMeshPrefix {
            subnet: Some(on_mesh_prefix_subnet.clone()),
            slaac_preferred: Some(true),
            slaac_valid: Some(true),
            ..fidl_fuchsia_lowpan_device::OnMeshPrefix::EMPTY
        };
        assert_eq!(driver.register_on_mesh_prefix(x.clone()).await, Ok(()));

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
        assert_eq!(driver.unregister_on_mesh_prefix(on_mesh_prefix_subnet.clone()).await, Ok(()));
        assert_eq!(driver.get_local_on_mesh_prefixes().await, Ok(vec![]));
    })
    .await;
}

#[fasync::run(10, test)]
async fn test_add_external_route() {
    test_harness(|driver| async move {
        assert_eq!(
            driver
                .provision_network(ProvisioningParams {
                    identity: TEST_IDENTITY.clone(),
                    credential: Some(Box::new(TEST_CREDENTIAL.clone())),
                })
                .await,
            Ok(())
        );

        driver.wait_for_state(DriverState::is_active_and_ready).await;

        let external_route_subnet: fidl_fuchsia_lowpan::Ipv6Subnet =
            Subnet { addr: std::net::Ipv6Addr::from_str("2001:DB8::").unwrap(), prefix_len: 64 }
                .into();
        let x = fidl_fuchsia_lowpan_device::ExternalRoute {
            subnet: Some(external_route_subnet.clone()),
            route_preference: Some(fidl_fuchsia_lowpan_device::RoutePreference::Medium),
            stable: Some(true),
            ..fidl_fuchsia_lowpan_device::ExternalRoute::EMPTY
        };
        assert_eq!(driver.register_external_route(x.clone()).await, Ok(()));

        fuchsia_async::Timer::new(fuchsia_async::Duration::from_millis(50)).await;

        assert_eq!(
            driver
                .get_local_external_routes()
                .map(|x| x.context("get_local_external_routes"))
                .and_then(|x| futures::future::ready(
                    x.first()
                        .ok_or(format_err!("local_external_routes is empty"))
                        .map(|x| x.subnet.clone())
                ))
                .await
                .unwrap(),
            x.subnet
        );
        assert_eq!(driver.unregister_external_route(external_route_subnet.clone()).await, Ok(()));
        assert_eq!(driver.get_local_external_routes().await, Ok(vec![]));
    })
    .await;
}

#[fasync::run(10, test)]
#[ignore] // TODO(fxrev.dev/92419): Remove this once <fxrev.dev/92419> is fixed.
async fn test_grind_lowpan_ot_driver() {
    test_harness(|driver| async move {
        let mut device_state_stream = driver.watch_device_state();
        let mut identity_stream = driver.watch_identity();

        assert_eq!(
            device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
            ConnectivityState::Offline
        );

        fx_log_debug!("app_task: Making sure it only vends one state when nothing has changed.");
        assert!(device_state_stream.next().now_or_never().is_none());

        for i in 1u8..32 {
            fx_log_debug!("app_task: Starting Iteration {}", i);

            fx_log_debug!("app_task: Setting disabled...");
            assert_eq!(driver.set_active(false).await, Ok(()));
            fx_log_debug!("app_task: Did disable!");

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
            assert_eq!(
                driver
                    .provision_network(ProvisioningParams {
                        identity: TEST_IDENTITY.clone(),
                        credential: Some(Box::new(TEST_CREDENTIAL.clone())),
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
                TEST_IDENTITY.raw_name.clone().map(ot::NetworkName::try_from)
            );
            assert_eq!(new_identity.xpanid, TEST_IDENTITY.xpanid);
            assert_eq!(new_identity.panid, TEST_IDENTITY.panid);
            assert_eq!(new_identity.channel, TEST_IDENTITY.channel);
            assert_eq!(new_identity.net_type, TEST_IDENTITY.net_type);
            fx_log_debug!("app_task: Identity is correct!");

            fx_log_debug!("app_task: Checking credential...");
            assert_eq!(driver.get_credential().await, Ok(Some(TEST_CREDENTIAL.clone())));
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

            fx_log_debug!("app_task: Setting disabled...");
            assert_eq!(driver.set_active(false).await, Ok(()));
            fx_log_debug!("app_task: Did disable!");

            fx_log_debug!("app_task: Checking device state... (Should be Inactive) (2)");
            assert_eq!(
                device_state_stream.try_next().await.unwrap().unwrap().connectivity_state.unwrap(),
                ConnectivityState::Inactive
            );

            fx_log_debug!("app_task: Finished Iteration {}", i);
        }
    })
    .await;
}
