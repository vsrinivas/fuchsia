// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    build_config::{BrEdrConfig, Config},
    host_dispatcher::{test as hd_test, HostDispatcher},
    store::stash::Stash,
    types,
};
use {
    anyhow::{format_err, Error},
    assert_matches::assert_matches,
    async_helpers::hanging_get::asynchronous as hanging_get,
    fidl::encoding::Decodable,
    fidl_fuchsia_bluetooth::Appearance,
    fidl_fuchsia_bluetooth_host::HostRequest,
    fidl_fuchsia_bluetooth_sys::{self as sys, TechnologyType},
    fuchsia_async::{self as fasync, futures::TryFutureExt, TimeoutExt},
    fuchsia_bluetooth::types::{
        bonding_data::example, Address, HostData, HostId, Identity, Peer, PeerId,
    },
    fuchsia_inspect::{self as inspect, assert_data_tree},
    futures::{channel::mpsc, future::join, stream::TryStreamExt, FutureExt},
    std::{
        collections::{HashMap, HashSet},
        path::Path,
    },
};

fn peer(id: PeerId) -> Peer {
    Peer {
        id: id.into(),
        address: Address::Public([1, 2, 3, 4, 5, 6]),
        technology: TechnologyType::LowEnergy,
        name: Some("Peer Name".into()),
        appearance: Some(Appearance::Watch),
        device_class: None,
        rssi: None,
        tx_power: None,
        connected: false,
        bonded: false,
        le_services: vec![],
        bredr_services: vec![],
    }
}

#[fasync::run_singlethreaded(test)]
async fn on_device_changed_inspect_state() {
    // test setup
    let stash = Stash::in_memory_mock();
    let inspector = inspect::Inspector::new();
    let system_inspect = inspector.root().create_child("system");
    let (gas_channel_sender, _generic_access_req_stream) = mpsc::channel(0);
    let watch_peers_broker = hanging_get::HangingGetBroker::new(
        HashMap::new(),
        |_, _| true,
        hanging_get::DEFAULT_CHANNEL_SIZE,
    );
    let watch_hosts_broker = hanging_get::HangingGetBroker::new(
        Vec::new(),
        |_, _| true,
        hanging_get::DEFAULT_CHANNEL_SIZE,
    );
    let dispatcher = HostDispatcher::new(
        "test".to_string(),
        Appearance::Display,
        stash,
        system_inspect,
        gas_channel_sender,
        watch_peers_broker.new_publisher(),
        watch_peers_broker.new_registrar(),
        watch_hosts_broker.new_publisher(),
        watch_hosts_broker.new_registrar(),
    );
    let peer_id = PeerId(1);

    // assert inspect tree is in clean state
    assert_data_tree!(inspector, root: {
        system: contains {
            peer_count: 0u64,
            peers: {}
        }
    });

    // add new peer and assert inspect tree is updated
    dispatcher.on_device_updated(peer(peer_id)).await;
    assert_data_tree!(inspector, root: {
        system: contains {
            peer_count: 1u64,
            peers: {
                "peer_0": contains {
                    peer_id: peer_id.to_string(),
                    technology: "LowEnergy"
                }
            }
        }
    });

    // remove peer and assert inspect tree is updated
    dispatcher.on_device_removed(peer_id).await;
    assert_data_tree!(inspector, root: {
        system: contains {
            peer_count: 0u64,
            peers: { }
        }
    });
}

#[fasync::run_singlethreaded(test)]
async fn test_change_name_no_deadlock() {
    let dispatcher = hd_test::make_simple_test_dispatcher();
    // Call a function that used to use the self.state.write().gas_channel_sender.send().await
    // pattern, which caused a deadlock by yielding to the executor while holding onto a write
    // lock to the mutable gas_channel. We expect an error here because there's no active host
    // in the dispatcher - we don't need to go through the trouble of setting up an emulated
    // host to test whether or not we can send messages to the GAS task. We just want to make
    // sure that the function actually returns and doesn't deadlock.
    assert_matches!(
        dispatcher.set_name("test-change".to_string()).await.unwrap_err(),
        types::Error::SysError(sys::Error::Failed)
    );
}

async fn host_is_in_dispatcher(id: &HostId, dispatcher: &HostDispatcher) -> bool {
    dispatcher.get_adapters().await.iter().map(|i| i.id).collect::<HashSet<_>>().contains(id)
}

#[fasync::run_singlethreaded(test)]
async fn apply_settings_fails_host_removed() {
    let dispatcher = hd_test::make_simple_test_dispatcher();
    let host_id = HostId(42);
    let mut host_server =
        hd_test::create_and_add_test_host_to_dispatcher(host_id, &dispatcher).unwrap();
    assert!(host_is_in_dispatcher(&host_id, &dispatcher).await);
    let run_host = async move {
        if let Ok(Some(HostRequest::SetConnectable { responder, .. })) =
            host_server.try_next().await
        {
            responder.send(&mut Err(sys::Error::Failed)).unwrap();
        } else {
            panic!("Unexpected request");
        }
    };
    let disable_connectable_fut = async {
        let updated_config = dispatcher
            .apply_sys_settings(sys::Settings {
                bredr_connectable_mode: Some(false),
                ..sys::Settings::new_empty()
            })
            .await;
        assert_matches!(
            updated_config,
            Config { bredr: BrEdrConfig { connectable: false, .. }, .. }
        );
    };
    futures::future::join(run_host, disable_connectable_fut).await;
    assert!(!host_is_in_dispatcher(&host_id, &dispatcher).await);
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_commit_bootstrap_doesnt_fail_from_host_failure() {
    // initiate test host-dispatcher
    let host_dispatcher = hd_test::make_simple_test_dispatcher();

    // add a test host with a channel we provide, which fails on restore_bonds()
    let host_id = HostId(1);
    let mut host_server =
        hd_test::create_and_add_test_host_to_dispatcher(host_id, &host_dispatcher).unwrap();
    assert!(host_is_in_dispatcher(&host_id, &host_dispatcher).await);

    let run_host = async {
        match host_server.try_next().await {
            Ok(Some(HostRequest::RestoreBonds { bonds, responder })) => {
                // Fail by returning all bonds as errors
                let _ = responder.send(&mut bonds.into_iter());
            }
            x => panic!("Expected RestoreBonds Request but got: {:?}", x),
        }
    };

    let identity = Identity {
        host: HostData { irk: None },
        bonds: vec![example::bond(
            Address::Public([1, 1, 1, 1, 1, 1]),
            Address::Public([0, 0, 0, 0, 0, 0]),
        )],
    };
    // Call dispatcher.commit_bootstrap() & assert that the result is success
    let result =
        futures::future::join(host_dispatcher.commit_bootstrap(vec![identity]), run_host).await.0;
    assert_matches!(result, Ok(()));
}

#[fuchsia_async::run_singlethreaded(test)]
async fn test_notify_host_watcher_of_active_hosts() {
    let watch_peers_broker = hanging_get::HangingGetBroker::new(
        HashMap::new(),
        |_, _| true,
        hanging_get::DEFAULT_CHANNEL_SIZE,
    );

    let watch_hosts_broker = hanging_get::HangingGetBroker::new(
        Vec::new(),
        crate::host_watcher::observe_hosts,
        hanging_get::DEFAULT_CHANNEL_SIZE,
    );

    let host_dispatcher = hd_test::make_test_dispatcher(
        watch_peers_broker.new_publisher(),
        watch_peers_broker.new_registrar(),
        watch_hosts_broker.new_publisher(),
        watch_hosts_broker.new_registrar(),
    );

    let watchers_fut = join(watch_peers_broker.run(), watch_hosts_broker.run()).map(|_| ());
    fasync::Task::spawn(watchers_fut).detach();

    // Start HostWatcher client/server.
    let (host_watcher_proxy, host_watcher_stream) =
        fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_bluetooth_sys::HostWatcherMarker>()
            .expect("fidl endpoints");
    let host_watcher_fut = crate::host_watcher::run(host_dispatcher.clone(), host_watcher_stream);
    let _host_watcher_task = fasync::Task::spawn(host_watcher_fut);

    // New host should be active by default.
    let host_id_0 = HostId(0);
    let _host_server_0 =
        hd_test::create_and_add_test_host_to_dispatcher(host_id_0, &host_dispatcher)
            .expect("add test host succeeds");
    assert!(host_is_in_dispatcher(&host_id_0, &host_dispatcher).await);

    // The future has a timeout so that tests consistently terminate.
    const HOST_WATCHER_TIMEOUT: std::time::Duration = std::time::Duration::from_secs(1);
    let hosts = host_watcher_proxy
        .watch()
        .map_err(|e| Error::from(e))
        .on_timeout(HOST_WATCHER_TIMEOUT, move || Err(format_err!("watch timed out")))
        .await
        .expect("watch timed out");
    assert_eq!(hosts.len(), 1);
    assert_matches!(hosts[0].active, Some(true));

    // Add a second host that should start inactive.
    let host_id_1 = HostId(1);
    let _host_server_1 =
        hd_test::create_and_add_test_host_to_dispatcher(host_id_1, &host_dispatcher)
            .expect("add test host succeeds");
    assert!(host_is_in_dispatcher(&host_id_1, &host_dispatcher).await);

    // The future has a timeout so that tests consistently terminate.
    let mut hosts = host_watcher_proxy
        .watch()
        .map_err(|e| Error::from(e))
        .on_timeout(HOST_WATCHER_TIMEOUT, move || Err(format_err!("watch timed out")))
        .await
        .expect("watch timed out");
    assert_eq!(hosts.len(), 2);
    hosts.sort_by(|a, b| a.id.unwrap().value.cmp(&b.id.unwrap().value));
    assert_eq!(hosts[0].id, Some(host_id_0.into()));
    assert_eq!(hosts[0].active, Some(true));
    assert_eq!(hosts[1].id, Some(host_id_1.into()));
    assert_eq!(hosts[1].active, Some(false));

    // Setting host 1 to active should set host 0 to inactive.
    assert_matches!(host_watcher_proxy.set_active(&mut host_id_1.into()).await, Ok(_));
    let mut hosts = host_watcher_proxy
        .watch()
        .map_err(|e| Error::from(e))
        .on_timeout(HOST_WATCHER_TIMEOUT, move || Err(format_err!("watch timed out")))
        .await
        .expect("watch timed out");
    assert_eq!(hosts.len(), 2);
    hosts.sort_by(|a, b| a.id.unwrap().value.cmp(&b.id.unwrap().value));
    assert_eq!(hosts[0].id, Some(host_id_0.into()));
    assert_eq!(hosts[0].active, Some(false));
    assert_eq!(hosts[1].id, Some(host_id_1.into()));
    assert_eq!(hosts[1].active, Some(true));

    // Removing active host 1 should mark host 0 as active.
    host_dispatcher.rm_device(&Path::new(&format!("/dev/host{}", host_id_1.0))).await;
    // The future has a timeout so that tests consistently terminate.
    let hosts = host_watcher_proxy
        .watch()
        .map_err(|e| Error::from(e))
        .on_timeout(HOST_WATCHER_TIMEOUT, move || Err(format_err!("watch timed out")))
        .await
        .expect("watch timed out");
    assert_eq!(hosts.len(), 1);
    assert_eq!(hosts[0].id, Some(host_id_0.into()));
    assert_eq!(hosts[0].active, Some(true));
}
