// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth as btfidl, fidl_fuchsia_bluetooth_sys as sys,
    fuchsia_bluetooth::types::{Peer, PeerId},
    log::warn,
    parking_lot::Mutex,
    std::collections::{HashMap, HashSet},
    std::sync::Arc,
};

pub struct PeerWatcher {
    // Store the previous state in a shared structure that can be updated when the hanging_get is
    // triggered
    last_seen: Arc<Mutex<HashMap<PeerId, Peer>>>,
    // The fidl responder to reply to when the state is updated
    responder: sys::AccessWatchPeersResponder,
}

impl PeerWatcher {
    // TODO(fxb/46830) - We need to define and determine sensible pagination. There's a maximum
    // channel message size (currently 64kb) on Fuchsia; we need to fit our response within that.
    // Currently we don't have maximum vector sizes on the fields within the `Peer` table (nor on
    // the response to this message), which makes it hard to measure accurately and usefully.
    //
    // In the meantime, 64 has been calculated as a maximum for both peers and IDs such that -
    // barring excessively long peer names and UUID vectors - we should safely fit inside the
    // message bounds.
    const MAX_PEERS_PER_WATCH: usize = 64;
    const MAX_PEERIDS_PER_WATCH: usize = 64;

    pub fn new(
        last_seen: Arc<Mutex<HashMap<PeerId, Peer>>>,
        responder: sys::AccessWatchPeersResponder,
    ) -> PeerWatcher {
        PeerWatcher { last_seen, responder }
    }

    // Written as an associated function in order to match the signature of the HangingGet
    pub fn observe(new_peers: &HashMap<PeerId, Peer>, watcher: PeerWatcher) -> bool {
        let mut last_seen = watcher.last_seen.lock();
        let (raw_updated, raw_removed) = peers_diff(&last_seen, new_peers);

        let pending_updates = raw_updated.len();
        let pending_removals = raw_removed.len();

        // If we can fit all messages in a single update, we have totally consumed the notification
        let consumed = (pending_updates <= PeerWatcher::MAX_PEERS_PER_WATCH)
            && (pending_removals <= PeerWatcher::MAX_PEERIDS_PER_WATCH);

        let raw_updated: Vec<_> =
            raw_updated.values().take(PeerWatcher::MAX_PEERS_PER_WATCH).collect();
        let raw_removed: Vec<_> =
            raw_removed.into_iter().take(PeerWatcher::MAX_PEERIDS_PER_WATCH).collect();

        let mut removed: Vec<btfidl::PeerId> = raw_removed.iter().map(|&p| p.into()).collect();
        let mut updated = raw_updated.iter().map(|p| (*p).into());

        if let Err(err) = watcher.responder.send(&mut updated, &mut removed.iter_mut()) {
            warn!("Unable to respond to watch_peers hanging get: {:?}", err);
        } else {
            // Apply only the truncated updates to our cache of the client's state; Updates that we
            // didn't send will need to be sent in the next update
            for peer in raw_updated {
                last_seen.insert(peer.id, peer.clone());
            }
            for peer in raw_removed {
                last_seen.remove(&peer);
            }
        }
        consumed
    }
}

fn peers_diff(
    prev: &HashMap<PeerId, Peer>,
    new: &HashMap<PeerId, Peer>,
) -> (HashMap<PeerId, Peer>, HashSet<PeerId>) {
    // Removed - those items in the prev set but not the new
    let removed: HashSet<_> = prev.keys().filter(|id| !new.contains_key(id)).cloned().collect();
    // Updated - those items which are not present in same configuration in the prev set
    let updated = new
        .into_iter()
        .filter(|(id, p)| !(prev.get(id) == Some(p)))
        .map(|(id, p)| (*id, p.clone()))
        .collect();
    (updated, removed)
}

#[cfg(test)]
mod test {
    use super::*;
    use {fuchsia_bluetooth::types::Address, futures::TryStreamExt};

    // Make some simple example peers for test cases
    fn example_peer(id: PeerId, address: Address, name: Option<String>) -> Peer {
        Peer {
            id,
            address,
            technology: sys::TechnologyType::DualMode,
            connected: false,
            bonded: false,
            name,
            appearance: None,
            device_class: None,
            rssi: None,
            tx_power: None,
            services: vec![],
        }
    }

    #[test]
    fn test_peers_diff() {
        let peer0 = example_peer(PeerId(0), Address::Public([0, 0, 0, 0, 0, 0]), None);
        let peer1 = example_peer(PeerId(1), Address::Public([1, 0, 0, 0, 0, 0]), None);
        let peer1b = example_peer(
            PeerId(1),
            Address::Public([1, 0, 0, 0, 0, 0]),
            Some("test-name".to_string()),
        );
        let peer2 = example_peer(PeerId(2), Address::Public([2, 0, 0, 0, 0, 0]), None);
        let peer3 = example_peer(PeerId(3), Address::Public([3, 0, 0, 0, 0, 0]), None);

        let before: HashMap<_, _> =
            vec![peer0.clone(), peer1, peer2.clone()].into_iter().map(|p| (p.id, p)).collect();
        // 0 is removed, 1 is changed, 2 is unchanged, 3 is added
        let after: HashMap<_, _> =
            vec![peer1b.clone(), peer2, peer3.clone()].into_iter().map(|p| (p.id, p)).collect();

        let (updated, removed) = peers_diff(&before, &after);

        // updated should be 1 and 3
        let expected_updated: HashMap<_, _> =
            vec![peer1b, peer3].into_iter().map(|p| (p.id, p)).collect();
        // Removed should be 0
        let expected_removed: HashSet<_> = vec![peer0].into_iter().map(|p| p.id).collect();

        assert_eq!(updated, expected_updated);
        assert_eq!(removed, expected_removed);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_observe() -> Result<(), anyhow::Error> {
        let (proxy, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<sys::AccessMarker>()?;
        let mut result_fut = proxy.watch_peers();
        let responder = requests
            .try_next()
            .await?
            .and_then(|o| o.into_watch_peers())
            .expect("must be watch peers");
        let last_seen = Arc::new(Mutex::new(HashMap::new()));
        let watcher = PeerWatcher::new(last_seen, responder);
        assert!(futures::poll!(&mut result_fut).is_pending());
        let new = HashMap::new();
        PeerWatcher::observe(&new, watcher);
        assert!(result_fut.await.is_ok());
        Ok(())
    }
}
