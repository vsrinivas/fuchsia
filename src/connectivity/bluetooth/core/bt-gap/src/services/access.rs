// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    async_helpers::hanging_get::asynchronous as hanging_get,
    fidl_fuchsia_bluetooth_sys::{self as sys, AccessRequest, AccessRequestStream},
    fuchsia_bluetooth::types::{
        pairing_options::{BondableMode, PairingOptions},
        Peer, PeerId, Technology,
    },
    futures::{
        future::{pending, BoxFuture},
        select, FutureExt, Stream, StreamExt,
    },
    log::{info, trace, warn},
    parking_lot::Mutex,
    std::{collections::HashMap, mem, sync::Arc},
};

use crate::{host_dispatcher::*, watch_peers::PeerWatcher};

#[derive(Default)]
struct AccessSession {
    peers_seen: Arc<Mutex<HashMap<PeerId, Peer>>>,
    /// Only one discovery session is stored per Access session at a time;
    /// if an Access client requests discovery while holding an existing session token,
    /// the old session is replaced, and the old session token is invalidated.
    discovery_session: Option<BoxFuture<'static, ()>>,
    discoverable_session: Option<BoxFuture<'static, ()>>,
}

pub async fn run(hd: HostDispatcher, mut stream: AccessRequestStream) -> Result<(), Error> {
    info!("fuchsia.bluetooth.sys.Access session started");
    let mut watch_peers_subscriber = hd.watch_peers().await;
    let mut session: AccessSession = Default::default();
    let mut discovery_pending = pending().boxed();
    let mut discoverable_pending = pending().boxed();

    loop {
        select! {
            event_opt = stream.next().fuse() => {
                match event_opt {
                    Some(event) => handler(hd.clone(), &mut watch_peers_subscriber, &mut session, event?).await?,
                    None => break,
                }
            }

            token_dropped = session.discovery_session.as_mut().unwrap_or(&mut discovery_pending).fuse() => {
                // drop the boxed future, which owns the discovery session token
                session.discovery_session = None;
            }

            token_dropped = session.discoverable_session.as_mut().unwrap_or(&mut discoverable_pending).fuse() => {
                session.discoverable_session = None;
            }
        };
    }

    info!("fuchsia.bluetooth.sys.Access session terminated");
    Ok(())
}

async fn handler(
    hd: HostDispatcher,
    watch_peers_subscriber: &mut hanging_get::Subscriber<PeerWatcher>,
    session: &mut AccessSession,
    request: AccessRequest,
) -> Result<(), Error> {
    match request {
        AccessRequest::SetPairingDelegate { input, output, delegate, control_handle } => {
            match delegate.into_proxy() {
                Ok(proxy) => {
                    hd.set_io_capability(input, output);
                    hd.set_pairing_delegate(proxy);
                }
                Err(err) => {
                    warn!(
                        "Ignoring Invalid Pairing Delegate passed to SetPairingDelegate: {}",
                        err
                    );
                    control_handle.shutdown()
                }
            }
            Ok(())
        }
        AccessRequest::SetLocalName { name, control_handle: _ } => {
            if let Err(e) = hd.set_name(name).await {
                warn!("Error setting local name: {:?}", e);
            }
            Ok(())
        }
        AccessRequest::SetDeviceClass { device_class, control_handle: _ } => {
            if let Err(e) = hd.set_device_class(device_class).await {
                warn!("Error setting local name: {:?}", e);
            }
            Ok(())
        }
        AccessRequest::MakeDiscoverable { token, responder } => {
            let stream = token.into_stream().unwrap(); // into_stream never fails
            let mut result = hd
                .set_discoverable()
                .await
                .map(|token| {
                    session.discoverable_session =
                        Some(watch_stream_for_session(stream, token).boxed());
                })
                .map_err(|e| e.into());
            responder.send(&mut result).map_err(Error::from)
        }
        AccessRequest::StartDiscovery { token, responder } => {
            let stream = token.into_stream().unwrap(); // into_stream never fails
            let mut result = hd
                .start_discovery()
                .await
                .map(|token| {
                    session.discovery_session =
                        Some(watch_stream_for_session(stream, token).boxed());
                })
                .map_err(|e| e.into());
            responder.send(&mut result).map_err(Error::from)
        }
        AccessRequest::WatchPeers { responder } => {
            watch_peers_subscriber
                .register(PeerWatcher::new(session.peers_seen.clone(), responder))
                .await
                .map_err(|e| {
                    // If we cannot register the observation, we return an error from the handler
                    // function. This terminates the stream and will drop the channel, as we are unable
                    // to fulfill our contract for WatchPeers(). The client can attempt to reconnect and
                    // if successful will receive a fresh session with initial state of the world
                    format_err!("Failed to watch peers: {:?}", e)
                })
        }
        AccessRequest::Connect { id, responder } => {
            let id = PeerId::from(id);
            let result = hd.connect(id).await;
            if let Err(e) = &result {
                warn!("Error connecting to peer {}: {:?}", id, e);
            }
            responder.send(&mut result.map_err(|e| e.into()))?;
            Ok(())
        }
        AccessRequest::Disconnect { id, responder } => {
            let id = PeerId::from(id);
            let result = hd.disconnect(id).await;
            if let Err(e) = &result {
                warn!("Error disconnecting from peer {}: {:?}", id, e);
            }
            responder.send(&mut result.map_err(|e| e.into()))?;
            Ok(())
        }
        AccessRequest::Pair { id, options, responder } => {
            let id = PeerId::from(id);
            let opts: PairingOptions = options.into();
            // We currently do not support NonBondable mode on the classic Br/Edr transport
            // If NonBondable is asked for a Br/Edr pairing, return an InvalidArguments error
            if opts.bondable == BondableMode::NonBondable && opts.transport == Technology::Classic {
                info!("Rejecting Pair: non-bondable mode not allowed for BR/EDR");
                responder.send(&mut Err(sys::Error::InvalidArguments))?;
                return Ok(());
            }
            let result = hd.pair(id, opts).await;
            if let Err(e) = &result {
                warn!("Error pairing with peer {}: {:?}", id, e);
            }
            let mut result = result.map_err(|e| match e.into() {
                sys::Error::PeerNotFound => sys::Error::PeerNotFound,
                sys::Error::InvalidArguments => sys::Error::InvalidArguments,
                // We map all other host errors to Error::Failed before reporting to the caller
                _ => sys::Error::Failed,
            });
            responder.send(&mut result)?;
            Ok(())
        }
        AccessRequest::Forget { id, responder } => {
            let id = PeerId::from(id);
            let result = hd.forget(id).await;
            if let Err(e) = &result {
                warn!("Error forgetting peer {}: {:?}", id, e);
            }
            responder.send(&mut result.map_err(|e| e.into()))?;
            Ok(())
        }
    }
}

async fn watch_stream_for_session<S: Stream + Send + 'static, T: Send + 'static>(
    stream: S,
    token: T,
) {
    stream.map(|_| ()).collect::<()>().await;
    // the remote end closed; drop our session token
    mem::drop(token);
    trace!("ProcedureToken dropped");
}
