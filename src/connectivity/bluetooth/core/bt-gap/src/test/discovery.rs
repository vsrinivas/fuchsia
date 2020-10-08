// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    host_device::{self, HostDevice},
    host_dispatcher,
    services::access,
};
use {
    fidl_fuchsia_bluetooth_host::HostMarker,
    fidl_fuchsia_bluetooth_sys::AccessMarker,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::{Address, HostId},
    futures::{future::join, stream::FuturesUnordered},
    parking_lot::RwLock,
    proptest::prelude::*,
    std::{collections::HashMap, fmt::Debug, iter, path::PathBuf, pin::Pin, sync::Arc},
};

// Maximum number of mock access.fidl clients we simulate.
// For a more comprehensive test, increase this number.
// For a faster running test, decrease this number.
const MAX_NUM_MOCK_CLIENTS: u64 = 4;

#[derive(Clone, Debug)]
enum Event {
    ToggleDiscovery(u64),
    Execute,
}

fn stream_events(client_no: u64) -> Vec<Event> {
    // For each client, we add two ToggleDiscovery events to the execution sequence
    // The first will start discovery, the second will stop discovery
    vec![Event::ToggleDiscovery(client_no), Event::ToggleDiscovery(client_no)]
}

/// A generated strategy is a Vector of Events, i.e. an execution sequence.
/// The number of clients is a random integer between 0 and max_num_clients.
/// An execution sequence is a random permutation of 4 events per client.
/// These 4 events are 2 ToggleDiscovery(client_no) events and 2 Execute events.
fn execution_sequences(max_num_clients: u64) -> impl Strategy<Value = Vec<Event>> {
    fn generate_events(num_clients: u64) -> Vec<Event> {
        let mut events =
            (0..num_clients).flat_map(|client_no| stream_events(client_no)).collect::<Vec<_>>();
        events.extend(iter::repeat(Event::Execute).take((num_clients * 2) as usize));
        events
    }

    (0..max_num_clients + 1).prop_map(generate_events).prop_shuffle()
}

fn run_access_and_host<F, T>(
    executor: &mut fuchsia_async::Executor,
    access_sessions: &mut FuturesUnordered<T>,
    host_task: &mut Pin<Box<F>>,
    host: host_device::HostDevice,
) -> bool
where
    T: futures::Future,
    F: futures::Future,
{
    for _ in 0..2 {
        // Run Access server sessions
        for mut access_fut in Pin::new(&mut *access_sessions).iter_pin_mut() {
            let _ = executor.run_until_stalled(&mut access_fut);
        }

        // Run mock host server
        let _ = executor.run_until_stalled(host_task);
    }

    // Send and process WatchHost request
    let _ = executor
        .run_until_stalled(&mut Box::pin(join(host.clone().refresh_test_host_info(), host_task)));

    host.info().discovering
}

proptest! {
    #[test]
    fn test_discovery_invariants(execution in execution_sequences(MAX_NUM_MOCK_CLIENTS)) {
        let mut executor = fasync::Executor::new().expect("error creating executor");
        let hd = host_dispatcher::test::make_simple_test_dispatcher()
            .expect("Failed to create test dispatcher");

        // Add mock host to dispatcher and make active
        let host_id = HostId(1);
        let host_address = Address::Public([0, 0, 0, 0, 0, 1]);
        let (host_proxy, host_stream) = fidl::endpoints::create_proxy_and_stream::<HostMarker>()?;
        let mock_host = HostDevice::mock(host_id, host_address, &PathBuf::from("/dev/host1"), host_proxy);
        let host_info = Arc::new(RwLock::new(mock_host.info()));

        hd.add_test_host(host_id, mock_host.clone());
        hd.set_active_host(host_id)?;

        // Maps {client no. -> discovery session token}
        let mut discovery_sessions = HashMap::new();
        // Maps {client no. -> access proxy}
        let mut access_proxies = HashMap::new();
        // For the collection of access::run futures
        let mut access_sessions = FuturesUnordered::new();
        // Host server future
        let mut host_task =
            Box::pin(host_device::test::run_discovery_host_server(host_stream, host_info));

        for event in execution {
            match event {
                Event::ToggleDiscovery(client_num) => {
                    match discovery_sessions.remove(&client_num) {
                        // send StartDiscovery request
                        None => {
                            let (access_proxy, access_stream) =
                                fidl::endpoints::create_proxy_and_stream::<AccessMarker>()?;
                            access_sessions.push(access::run(hd.clone(), access_stream));

                            let (discovery_session, discovery_session_server) =
                                fidl::endpoints::create_proxy()
                                    .expect("failure creating fidl proxy");

                            let _ = executor.run_until_stalled(
                                &mut access_proxy.start_discovery(discovery_session_server),
                            );

                            discovery_sessions.insert(client_num, discovery_session);
                            access_proxies.insert(client_num, access_proxy);
                        }

                        // drop existing discovery session
                        Some(discovery_session) => {
                            drop(discovery_session);
                        }
                    }
                }

                Event::Execute => {
                    let is_discovering = run_access_and_host(
                        &mut executor,
                        &mut access_sessions,
                        &mut host_task,
                        mock_host.clone(),
                    );
                    // INVARIANT:
                    // If discovery_sessions is nonempty, at least one client holds a discovery
                    // session token, so host should be discovering. Otherwise, host should not
                    // be discovering.

                    // We test our invariant during Execute events, after driving the Access
                    // and host futures, to ensure there are no pending requests, the
                    // processessing of which may affect the system's state.
                    prop_assert_eq!(is_discovering, !discovery_sessions.is_empty());
                }
            }
        }

        // Drop remaining discovery sessions and ensure host stops discovery
        drop(discovery_sessions);
        let is_discovering = run_access_and_host(
            &mut executor,
            &mut access_sessions,
            &mut host_task,
            mock_host.clone(),
        );
        prop_assert!(!is_discovering);
    }
}
