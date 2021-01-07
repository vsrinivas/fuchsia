// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::generator::Generator,
    crate::probe_node::{probe_node, Selector},
    anyhow::{format_err, Error},
    fidl_fuchsia_overnet_protocol::NodeId,
    fuchsia_async::{TimeoutExt, Timer},
    futures::future::Either,
    futures::lock::Mutex,
    futures::prelude::*,
    hoist::{hoist, OvernetInstance},
    std::collections::HashSet,
    std::sync::Arc,
    std::time::Duration,
};

// List peers, but wait for things to settle out first
pub fn list_peers() -> impl Stream<Item = Result<NodeId, Error>> {
    // On Fuchsia, we'll connect to overnetstack, and so we should at least see that.
    #[cfg(target_os = "fuchsia")]
    const MIN_PEERS: usize = 1;
    // Whereas on host, we'll connect to ascendd from the overnet library, so we should see the current process & ascendd at least.
    #[cfg(not(target_os = "fuchsia"))]
    const MIN_PEERS: usize = 2;

    Generator::new(move |mut tx| async move {
        let r: Result<(), Error> = async {
            let svc = hoist().connect_as_service_consumer()?;
            let seen_peers = Arc::new(Mutex::new(HashSet::new()));
            let more_to_do = {
                let seen_peers = seen_peers.clone();
                move || async move {
                    let seen_peers = Arc::new(seen_peers.lock().await.clone());
                    log::trace!("check if more to do after seeing: {:?}", seen_peers);
                    if seen_peers.len() < MIN_PEERS {
                        return true;
                    }
                    futures::future::select_ok(seen_peers.iter().map(|&id| {
                        let seen_peers = seen_peers.clone();
                        async move {
                            // This async block returns Ok(()) if there is more work to do, and Err(()) if no new work is detected.
                            // The outer select_ok will return Ok(()) if *any* child returns Ok(()), and Err(()) if *all* children return Err(()).
                            log::trace!("check for new peers with {}", id);
                            match probe_node(NodeId { id }, Selector::Links).on_timeout(Duration::from_secs(5), || Err(format_err!("timeout waiting for diagnostic probe"))). await {
                                Ok(links) => {
                                    for link in links.links.unwrap_or_else(Vec::new) {
                                        if let Some(destination) = link.destination {
                                            if !seen_peers.contains(&destination.id) {
                                                log::trace!(
                                                    "note new destination: {:?} via link {:?} from {}",
                                                    destination.id, link, id
                                                );
                                                return Ok(());
                                            }
                                        }
                                    }
                                    log::trace!("checked for new peers with {}", id);
                                    Err(())
                                }
                                Err(e) => {
                                    log::trace!("Seen node {:?} failed probe: {:?}", id, e);
                                    Err(())
                                }
                            }
                        }
                        .boxed()
                    }))
                    .await
                    .is_ok()
                }
            };
            log::trace!("list_peers begins");
            loop {
                let more_to_do = more_to_do.clone();
                let wait_until_no_more_to_do = async move {
                    while (more_to_do.clone())().await {
                        Timer::new(Duration::from_secs(1)).await;
                    }
                };
                match futures::future::select(
                    wait_until_no_more_to_do.boxed(),
                    svc.list_peers().boxed(),
                )
                .await
                {
                    Either::Left(((), _)) => break,
                    Either::Right((Ok(peers), wait_until_no_more_to_do)) => {
                        drop(wait_until_no_more_to_do);
                        let mut seen_peers = seen_peers.lock().await;
                        for peer in peers.into_iter() {
                            if seen_peers.insert(peer.id.id) {
                                tx.send(Ok(peer.id.into())).await?;
                            }
                        }
                    }
                    Either::Right((Err(e), _)) => return Err(e.into()),
                }
            }
            log::trace!("list_peers done");
            Ok(())
        }
        .await;
        if let Err(e) = r {
            let _ = tx.send(Err(e)).await;
        }
    })
}

/// Get this nodes id
pub async fn own_id() -> Result<NodeId, Error> {
    for peer in hoist().connect_as_service_consumer()?.list_peers().await?.into_iter() {
        if peer.is_self {
            return Ok(peer.id);
        }
    }
    Err(anyhow::format_err!("Cannot find myself"))
}

/// Given a string get a list of peers
/// String could be a comma separated list of node-ids, or the keyword 'all' to retrieve all peers
pub fn list_peers_from_argument(
    argument: &str,
) -> Result<impl Stream<Item = Result<NodeId, Error>>, Error> {
    let filter = parse_peers_argument(argument)?;
    Ok(list_peers().try_filter(move |n| {
        futures::future::ready(match &filter {
            PeerList::All => true,
            PeerList::Exactly(peers) => peers.contains(&n.id),
        })
    }))
}

enum PeerList {
    All,
    Exactly(HashSet<u64>),
}

fn parse_peers_argument(argument: &str) -> Result<PeerList, Error> {
    let argument = argument.trim();
    if argument == "all" {
        return Ok(PeerList::All);
    }
    let r: Result<HashSet<u64>, Error> =
        argument.split(',').map(|s| s.trim().parse().map_err(Into::into)).collect();
    Ok(PeerList::Exactly(r?))
}
