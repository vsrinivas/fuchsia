// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_bluetooth_sys::{AccessMarker, AccessProxy},
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectationHarness},
        types::{Peer, PeerId},
    },
    futures::future::{self, BoxFuture, FutureExt},
    std::{collections::HashMap, convert::TryInto},
};

use crate::harness::TestHarness;

#[derive(Clone, Default)]
pub struct AccessState {
    /// Remote Peers seen
    pub peers: HashMap<PeerId, Peer>,
}

pub type AccessHarness = ExpectationHarness<AccessState, AccessProxy>;

async fn watch_peers(harness: AccessHarness) -> Result<(), Error> {
    let proxy = harness.aux().clone();
    loop {
        let (updated, removed) =
            proxy.watch_peers().await.context("Error calling Access.watch_peers()")?;
        for peer in updated.into_iter() {
            let peer: Peer = peer.try_into().context("Invalid peer received from WatchPeers()")?;
            harness.write_state().peers.insert(peer.id, peer);
        }
        for id in removed.into_iter() {
            harness.write_state().peers.remove(&id.into());
        }
        harness.notify_state_changed();
    }
}

pub async fn new_access_harness() -> Result<AccessHarness, Error> {
    let proxy = fuchsia_component::client::connect_to_service::<AccessMarker>()
        .context("Failed to connect to access service")?;

    Ok(AccessHarness::new(proxy))
}

impl TestHarness for AccessHarness {
    type Env = ();
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let harness = new_access_harness().await?;
            let run_access = watch_peers(harness.clone()).boxed();
            Ok((harness, (), run_access))
        }
        .boxed()
    }
    fn terminate(_env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        future::ok(()).boxed()
    }
}

pub mod expectation {
    use crate::harness::{access::AccessState, host_watcher::HostWatcherState};
    use fuchsia_bluetooth::{
        expectation::Predicate,
        types::{Address, HostId, HostInfo, Peer, PeerId, Uuid},
    };

    mod peer {
        use super::*;

        pub(crate) fn exists(p: Predicate<Peer>) -> Predicate<AccessState> {
            let msg = format!("peer exists satisfying {:?}", p);
            Predicate::predicate(
                move |state: &AccessState| state.peers.iter().any(|(_, d)| p.satisfied(d)),
                &msg,
            )
        }

        pub(crate) fn with_identifier(id: PeerId) -> Predicate<Peer> {
            Predicate::<Peer>::predicate(move |d| d.id == id, &format!("identifier == {}", id))
        }

        pub(crate) fn with_address(address: Address) -> Predicate<Peer> {
            Predicate::<Peer>::predicate(
                move |d| d.address == address,
                &format!("address == {}", address),
            )
        }

        pub(crate) fn connected(connected: bool) -> Predicate<Peer> {
            Predicate::<Peer>::predicate(
                move |d| d.connected == connected,
                &format!("connected == {}", connected),
            )
        }

        pub(crate) fn with_bredr_service(service_uuid: Uuid) -> Predicate<Peer> {
            let msg = format!("bredr_services.contains({})", service_uuid.to_string());
            Predicate::<Peer>::predicate(move |d| d.bredr_services.contains(&service_uuid), &msg)
        }
    }

    mod host {
        use super::*;

        pub(crate) fn with_name<S: ToString>(name: S) -> Predicate<HostInfo> {
            let name = name.to_string();
            let msg = format!("name == {}", name);
            Predicate::<HostInfo>::predicate(move |h| h.local_name.as_ref() == Some(&name), &msg)
        }

        pub(crate) fn with_id(id: HostId) -> Predicate<HostInfo> {
            let msg = format!("id == {}", id);
            Predicate::<HostInfo>::predicate(move |h| h.id == id, &msg)
        }

        pub(crate) fn discovering(is_discovering: bool) -> Predicate<HostInfo> {
            let msg = format!("discovering == {}", is_discovering);
            Predicate::<HostInfo>::predicate(move |h| h.discovering == is_discovering, &msg)
        }

        pub(crate) fn discoverable(is_discoverable: bool) -> Predicate<HostInfo> {
            let msg = format!("discoverable == {}", is_discoverable);
            Predicate::<HostInfo>::predicate(move |h| h.discoverable == is_discoverable, &msg)
        }

        pub(crate) fn exists(p: Predicate<HostInfo>) -> Predicate<HostWatcherState> {
            let msg = format!("Host exists satisfying {:?}", p);
            Predicate::predicate(
                move |state: &HostWatcherState| state.hosts.values().any(|h| p.satisfied(h)),
                &msg,
            )
        }
    }

    pub fn peer_connected(id: PeerId, connected: bool) -> Predicate<AccessState> {
        peer::exists(peer::with_identifier(id).and(peer::connected(connected)))
    }

    pub fn peer_with_address(address: Address) -> Predicate<AccessState> {
        peer::exists(peer::with_address(address))
    }

    pub fn host_with_name<S: ToString>(name: S) -> Predicate<HostWatcherState> {
        host::exists(host::with_name(name))
    }

    pub fn peer_bredr_service_discovered(id: PeerId, service_uuid: Uuid) -> Predicate<AccessState> {
        peer::exists(peer::with_identifier(id).and(peer::with_bredr_service(service_uuid)))
    }

    pub fn host_discovering(id: HostId, is_discovering: bool) -> Predicate<HostWatcherState> {
        host::exists(host::with_id(id).and(host::discovering(is_discovering)))
    }
    pub fn host_discoverable(id: HostId, is_discoverable: bool) -> Predicate<HostWatcherState> {
        host::exists(host::with_id(id).and(host::discoverable(is_discoverable)))
    }
}
