// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_helpers::maybe_stream::MaybeStream;
use async_utils::stream::FutureMap;
use core::{
    pin::Pin,
    task::{Context, Poll},
};
use fidl::client::QueryResponseFut;
use fidl::endpoints::{ControlHandle, Proxy, RequestStream};
use fidl_fuchsia_bluetooth_le::{
    AdvertisedPeripheralMarker, AdvertisedPeripheralRequest, AdvertisedPeripheralRequestStream,
    AdvertisingData, AdvertisingModeHint, AdvertisingParameters, ConnectionOptions,
    PeripheralError, PeripheralMarker, PeripheralProxy, ServiceData,
};
use fuchsia_bluetooth::types::{le::Peer, PeerId, Uuid};
use futures::stream::{FusedStream, Stream, StreamExt};
use futures::{ready, Future, FutureExt};
use std::convert::TryFrom;
use tracing::{debug, info, trace, warn};

use crate::error::Error;
use crate::gatt_service::FAST_PAIR_SERVICE_UUID;
use crate::types::{AccountKeyList, ModelId};

/// Item type returned by `<LowEnergyAdvertiser as Stream>::poll_next`.
#[derive(Debug)]
pub enum LowEnergyEvent {
    /// A remote peer has connected to the local device.
    PeerConnected { id: PeerId },

    /// A connected remote peer has disconnected from the local device.
    PeerDisconnected { id: PeerId },

    /// The current LE Advertisement has terminated.
    AdvertisementTerminated,
}

/// A Future associated with an LE connection to a remote peer.
type ConnectionFut = Pin<Box<dyn Future<Output = PeerId>>>;

fn flatten_err<T, E, F>(result: Result<Result<T, E>, F>) -> Result<T, Error>
where
    Error: From<E>,
    Error: From<F>,
{
    Ok(result??)
}

/// An active Low Energy advertisement.
/// `LowEnergyAdvertisement` implements `Stream` and produces `LowEnergyEvent`s. The
/// `LowEnergyAdvertisement` stream _must_ be polled in order to process incoming connection &
/// disconnection requests from remote peers.
/// Note: The lifetime of the Stream is tied to the `advertise_fut`. This is a direct consequence of
/// the invariants of the `le.Peripheral` API. Specifically, this is to prevent clients from
/// restarting an advertisement before the previous one has been closed completely & processed by
/// the lower layers of the Bluetooth stack.
/// NOTE: Before discarding the advertisement, use `LowEnergyAdvertisement::stop` to correctly stop
/// the advertisement and clean up state.
struct LowEnergyAdvertisement {
    /// The Future associated with the LE Advertise request. Per the `le.Peripheral` API docs, the
    /// lifetime of the Advertisement is tied to this Future.
    advertise_fut: QueryResponseFut<Result<(), PeripheralError>>,
    /// A stream of incoming connection requests that are initiated by the remote peer.
    incoming_connections: AdvertisedPeripheralRequestStream,
    /// The termination status of the advertisement. This is determined by the status of the
    /// `advertise_fut`.
    terminated: bool,
}

impl LowEnergyAdvertisement {
    fn new(
        advertise_fut: QueryResponseFut<Result<(), PeripheralError>>,
        incoming_connections: AdvertisedPeripheralRequestStream,
    ) -> Self {
        Self { advertise_fut, incoming_connections, terminated: false }
    }

    fn handle_advertised_peripheral_request(
        &mut self,
        request: AdvertisedPeripheralRequest,
    ) -> Option<(PeerId, ConnectionFut)> {
        let AdvertisedPeripheralRequest::OnConnected { peer, connection, responder } = request;
        let _ = responder.send();
        debug!("{:?} connected over LE", peer);

        let peer = match Peer::try_from(peer) {
            Ok(peer) => peer,
            Err(e) => {
                warn!("Invalidly formatted peer: {:?}", e);
                return None;
            }
        };

        // Keep the `connection` handle alive so that the LE connection is persisted.
        let connection = match connection.into_proxy() {
            Ok(conn) => conn,
            Err(e) => {
                warn!("ConnectionProxy already closed: {:?}", e);
                return None;
            }
        };

        let id = peer.id;
        // Resolves when the remote peer disconnects.
        let closed_fut = Box::pin(async move {
            let _ = connection.on_closed().await;
            debug!("{:?} disconnected", id);
            id
        });
        Some((id, closed_fut))
    }

    /// Closes the active LE advertisement. Returns the result of the Advertise request.
    async fn stop(self) -> Result<(), Error> {
        // If the LowEnergyAdvertisement stream is already terminated, then there is no work to be
        // done.
        if self.is_terminated() {
            return Ok(());
        }

        // Otherwise, close the AdvertisedPeripheral channel to signal termination. The upstream LE
        // server will detect this, process closure, and resolve the `advertise_fut`.
        self.incoming_connections.control_handle().shutdown();
        drop(self.incoming_connections);
        let result = self.advertise_fut.await;
        flatten_err(result)
    }
}

impl Stream for LowEnergyAdvertisement {
    type Item = (PeerId, ConnectionFut);

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.terminated {
            panic!("Cannot poll a terminated stream");
        }

        if let Poll::Ready(result) = self.advertise_fut.poll_unpin(cx).map(flatten_err) {
            // Per the `le.Peripheral` docs, the lifetime of the advertisement is tied to the
            // `advertise_fut`.
            self.terminated = true;
            debug!("Low Energy advertisement finished with result: {:?}", result);
            return Poll::Ready(None);
        }

        // This check prevents polling `incoming_connections` after termination.
        // Context: There may be a small window between the `le.Peripheral` server closing
        // `incoming_connections` to signal termination and the server responding to the
        // `advertise_fut`.
        if !self.incoming_connections.is_terminated() {
            // Poll the incoming connection stream until an item is yielded or Pending is returned.
            loop {
                let result = ready!(self.incoming_connections.poll_next_unpin(cx));
                match result {
                    Some(Ok(update)) => match self.handle_advertised_peripheral_request(update) {
                        None => continue,
                        event => return Poll::Ready(event),
                    },
                    Some(Err(e)) => {
                        warn!("Error in AdvertisedPeripheral FIDL client request: {}. Closing", e);
                        self.incoming_connections.control_handle().shutdown();
                        // The `advertise_fut` should resolve once the upstream LE server has
                        // detected channel closure.
                        break;
                    }
                    None => {
                        trace!("AdvertisedPeripheral stream exhausted before `advertise_fut`.");
                        break;
                    }
                }
            }
        }
        Poll::Pending
    }
}

impl FusedStream for LowEnergyAdvertisement {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

/// Abstraction over the `le.Peripheral` FIDL API for starting, managing, and tracking events from
/// Bluetooth Low Energy advertising.
pub struct LowEnergyAdvertiser {
    /// Connection to the `le.Peripheral` protocol.
    peripheral: PeripheralProxy,
    /// Represents an active advertisement. If set, this contains the stream associated with the
    /// active advertisement.
    advertisement: MaybeStream<LowEnergyAdvertisement>,
    /// The current set of active LE connections. This is persisted independently of the active
    /// advertisement.
    active_connections: FutureMap<PeerId, ConnectionFut>,
}

impl LowEnergyAdvertiser {
    pub fn new() -> Result<Self, Error> {
        let peripheral = fuchsia_component::client::connect_to_protocol::<PeripheralMarker>()?;
        Ok(Self::from_proxy(peripheral))
    }

    pub fn from_proxy(peripheral: PeripheralProxy) -> Self {
        Self { peripheral, advertisement: Default::default(), active_connections: FutureMap::new() }
    }

    /// Attempts to start advertising the Fast Pair device `model_id` over Low Energy.
    /// Clears the existing advertisement, if set.
    /// Returns Ok on success, or Error if advertising was unable to be started for any reason.
    pub async fn advertise_model_id(&mut self, model_id: ModelId) -> Result<(), Error> {
        let _ = self.stop_advertising().await;

        let model_id_bytes: [u8; 3] = model_id.into();
        self.start_advertising(model_id_bytes.to_vec(), AdvertisingModeHint::VeryFast)
    }

    /// Attempts to start advertising the set of Fast Pair Account `keys` over Low Energy.
    /// Clears the existing advertisement, if set.
    /// Returns Ok on success, or Error if advertising was unable to be started for any reason.
    pub async fn advertise_account_keys(&mut self, keys: &AccountKeyList) -> Result<(), Error> {
        let _ = self.stop_advertising().await;

        // First byte is reserved (0x00).
        let mut advertisement_bytes = vec![0];
        // Next bytes are the formatted account key service data.
        advertisement_bytes.extend(keys.service_data()?);

        self.start_advertising(advertisement_bytes, AdvertisingModeHint::Fast)
    }

    /// Clears the active advertisement.
    /// Returns true if there is an existing advertisement that is stopped, false otherwise.
    pub async fn stop_advertising(&mut self) -> bool {
        if let Some(active_advertisement) = MaybeStream::take(&mut self.advertisement) {
            let result = active_advertisement.stop().await;
            info!("Stopped LE advertisement with result: {:?}", result);
            return true;
        }
        false
    }

    fn start_advertising(
        &mut self,
        service_data_bytes: Vec<u8>,
        mode: AdvertisingModeHint,
    ) -> Result<(), Error> {
        let parameters = AdvertisingParameters {
            data: Some(AdvertisingData {
                service_uuids: Some(vec![Uuid::new16(FAST_PAIR_SERVICE_UUID).into()]),
                service_data: Some(vec![ServiceData {
                    uuid: Uuid::new16(FAST_PAIR_SERVICE_UUID).into(),
                    data: service_data_bytes,
                }]),
                include_tx_power_level: Some(true),
                ..AdvertisingData::EMPTY
            }),
            connection_options: Some(ConnectionOptions {
                bondable_mode: Some(true),
                ..ConnectionOptions::EMPTY
            }),
            mode_hint: Some(mode),
            ..AdvertisingParameters::EMPTY
        };

        let (connect_client, connect_server) =
            fidl::endpoints::create_request_stream::<AdvertisedPeripheralMarker>()?;
        let advertise_fut = self.peripheral.advertise(parameters, connect_client);

        let advertisement_stream = LowEnergyAdvertisement::new(advertise_fut, connect_server);
        self.advertisement.set(advertisement_stream);
        Ok(())
    }
}

impl Stream for LowEnergyAdvertiser {
    type Item = LowEnergyEvent;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        // Check the active advertisement.
        let update = self.advertisement.poll_next_unpin(cx);
        match update {
            Poll::Pending => (),
            Poll::Ready(Some((id, connection))) => {
                let _ = self.active_connections.insert(id, connection);
                return Poll::Ready(Some(LowEnergyEvent::PeerConnected { id }));
            }
            Poll::Ready(None) => {
                // Otherwise, the advertisement is finished. Reset the stream and let the caller
                // know.
                self.advertisement = Default::default();
                return Poll::Ready(Some(LowEnergyEvent::AdvertisementTerminated));
            }
        }

        // Check to see if any active connections have finished. This indicates a peer disconnection
        if !self.active_connections.is_terminated() {
            if let Poll::Ready(Some(id)) = self.active_connections.poll_next_unpin(cx) {
                return Poll::Ready(Some(LowEnergyEvent::PeerDisconnected { id }));
            }
        }

        // Otherwise, either no active connection is finished or the FutureMap is exhausted (e.g
        // empty). This is OK as we expect it to be populated with future connections.
        Poll::Pending
    }
}

impl FusedStream for LowEnergyAdvertiser {
    fn is_terminated(&self) -> bool {
        self.advertisement.is_terminated()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use async_test_helpers::{expect_stream_item, expect_stream_pending, run_while};
    use async_utils::PollExt;
    use fidl_fuchsia_bluetooth_le::{
        self as le, AdvertisedPeripheralProxy, ConnectionMarker, PeripheralAdvertiseResponder,
        PeripheralRequestStream,
    };
    use fuchsia_async as fasync;
    use futures::{pin_mut, stream::StreamExt};

    use crate::types::AccountKey;

    fn make_model_id_advertise(
        exec: &mut fasync::TestExecutor,
        advertiser: &mut LowEnergyAdvertiser,
    ) -> Result<(), Error> {
        let model_id = ModelId::try_from(3).unwrap();
        let adv_fut = advertiser.advertise_model_id(model_id);
        pin_mut!(adv_fut);
        exec.run_until_stalled(&mut adv_fut).expect("should be able to advertise")
    }

    #[track_caller]
    fn expect_advertise_request(
        exec: &mut fasync::TestExecutor,
        stream: &mut PeripheralRequestStream,
    ) -> (AdvertisedPeripheralProxy, PeripheralAdvertiseResponder) {
        let adv_request = exec
            .run_until_stalled(&mut stream.next())
            .expect("request ready")
            .expect("valid FIDL")
            .unwrap();
        let (_, adv_peripheral_client, responder) =
            adv_request.into_advertise().expect("Peripheral.Advertise");
        (adv_peripheral_client.into_proxy().unwrap(), responder)
    }

    #[fuchsia::test]
    fn advertisement_terminating_early_results_in_stream_item() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (c, mut upstream_server) =
            fidl::endpoints::create_proxy_and_stream::<PeripheralMarker>().unwrap();
        let mut advertiser = LowEnergyAdvertiser::from_proxy(c);
        // Initially, with no advertisement, the advertiser Stream shouldn't be terminated.
        assert!(!advertiser.is_terminated());
        expect_stream_pending(&mut exec, &mut advertiser);

        // Make a request to advertise the Model ID.
        let result = make_model_id_advertise(&mut exec, &mut advertiser);
        assert_matches!(result, Ok(_));
        expect_stream_pending(&mut exec, &mut advertiser);

        // Expect upstream LE server to receive advertise request.
        let (adv_peripheral_client, responder) =
            expect_advertise_request(&mut exec, &mut upstream_server);
        // Polling the LE Advertiser should yield no stream items as no LE events have occurred.
        expect_stream_pending(&mut exec, &mut advertiser);

        // Upstream server no longer can manage this advertisement.
        drop(adv_peripheral_client);
        // The upstream server hasn't responded to the Advertise request yet, which means the status
        // of the advertisement is not fully terminated (though we expect it to be shortly).
        expect_stream_pending(&mut exec, &mut advertiser);
        // Upstream finally is ready to stop advertising.
        let _ = responder.send(&mut Err(PeripheralError::Aborted));

        let le_event = expect_stream_item(&mut exec, &mut advertiser);
        assert_matches!(le_event, LowEnergyEvent::AdvertisementTerminated);

        // The advertiser itself is not terminated because the MaybeStream is reset upon
        // advertisement termination.
        assert!(!advertiser.is_terminated());
        // Stopping advertising is a no-op since the advertisement was already terminated.
        let stop_fut = advertiser.stop_advertising();
        pin_mut!(stop_fut);
        assert!(!exec.run_until_stalled(&mut stop_fut).expect("resolves immediately"));
    }

    #[fuchsia::test]
    fn subsequent_advertise_request_stops_previous() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (c, mut upstream_server) =
            fidl::endpoints::create_proxy_and_stream::<PeripheralMarker>().unwrap();
        let mut advertiser = LowEnergyAdvertiser::from_proxy(c);

        // Make a request to advertise the Model ID.
        make_model_id_advertise(&mut exec, &mut advertiser).expect("can advertise");
        expect_stream_pending(&mut exec, &mut advertiser);
        let (adv_peripheral_client, responder) =
            expect_advertise_request(&mut exec, &mut upstream_server);
        assert!(!adv_peripheral_client.is_closed());
        expect_stream_pending(&mut exec, &mut advertiser);

        // Make another request to advertise the Account Keys - this time manually because stopping the
        // previous advertisement is async.
        let (_client, _responder) = {
            let example_account_keys =
                AccountKeyList::with_capacity_and_keys(1, vec![AccountKey::new([1; 16])]);
            let adv_fut = advertiser.advertise_account_keys(&example_account_keys);
            pin_mut!(adv_fut);
            let _ =
                exec.run_until_stalled(&mut adv_fut).expect_pending("waiting for stop advertise");

            // Upstream LE server should detect the stop request and respond to process termination.
            let closed_fut = adv_peripheral_client.on_closed();
            pin_mut!(closed_fut);
            let (closed_result, mut adv_fut) = run_while(&mut exec, adv_fut, closed_fut);
            assert_matches!(closed_result, Ok(_));
            let _ = responder.send(&mut Ok(())).unwrap();

            // Second advertise request should be OK and received by upstream LE server.
            let result = exec.run_until_stalled(&mut adv_fut).expect("advertise is ready");
            assert_matches!(result, Ok(_));
            expect_advertise_request(&mut exec, &mut upstream_server)
        };

        // Meanwhile, no LE events have occurred.
        assert!(!advertiser.is_terminated());
        expect_stream_pending(&mut exec, &mut advertiser);
    }

    #[fuchsia::test]
    fn le_connection_events_propagated_to_stream() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (c, mut upstream_server) =
            fidl::endpoints::create_proxy_and_stream::<PeripheralMarker>().unwrap();
        let mut advertiser = LowEnergyAdvertiser::from_proxy(c);

        make_model_id_advertise(&mut exec, &mut advertiser).expect("can advertise");
        let (adv_peripheral_client, _responder) =
            expect_advertise_request(&mut exec, &mut upstream_server);
        expect_stream_pending(&mut exec, &mut advertiser);

        // Upstream server notifies of two incoming LE connections.
        let example_peer1 =
            le::Peer { id: Some(PeerId(123).into()), connectable: Some(true), ..le::Peer::EMPTY };
        let (connect_client1, connect_server1) =
            fidl::endpoints::create_request_stream::<ConnectionMarker>().unwrap();
        let connected_fut1 = adv_peripheral_client.on_connected(example_peer1, connect_client1);
        pin_mut!(connected_fut1);
        let _ = exec
            .run_until_stalled(&mut connected_fut1)
            .expect_pending("waiting for LEAdvertiser response");

        let example_peer2 =
            le::Peer { id: Some(PeerId(987).into()), connectable: Some(true), ..le::Peer::EMPTY };
        let (connect_client2, connect_server2) =
            fidl::endpoints::create_request_stream::<ConnectionMarker>().unwrap();
        let connected_fut2 = adv_peripheral_client.on_connected(example_peer2, connect_client2);
        pin_mut!(connected_fut2);
        let _ = exec
            .run_until_stalled(&mut connected_fut2)
            .expect_pending("waiting for LEAdvertiser response");

        // Connection event should be produced in the Advertiser stream.
        let le_event = expect_stream_item(&mut exec, &mut advertiser);
        assert_matches!(le_event, LowEnergyEvent::PeerConnected { .. });
        let le_event = expect_stream_item(&mut exec, &mut advertiser);
        assert_matches!(le_event, LowEnergyEvent::PeerConnected { .. });
        let _ = exec.run_until_stalled(&mut connected_fut1).expect("receive response");
        let _ = exec.run_until_stalled(&mut connected_fut2).expect("receive response");

        // Both Peers disconnect at similar times - expect Disconnected events for both.
        drop(connect_server1);
        drop(connect_server2);
        let le_event = expect_stream_item(&mut exec, &mut advertiser);
        assert_matches!(le_event, LowEnergyEvent::PeerDisconnected { .. });
        let le_event = expect_stream_item(&mut exec, &mut advertiser);
        assert_matches!(le_event, LowEnergyEvent::PeerDisconnected { .. });
    }

    #[fuchsia::test]
    fn invalidly_formatted_peer_connection_produces_no_event() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (c, mut upstream_server) =
            fidl::endpoints::create_proxy_and_stream::<PeripheralMarker>().unwrap();
        let mut advertiser = LowEnergyAdvertiser::from_proxy(c);

        make_model_id_advertise(&mut exec, &mut advertiser).expect("can advertise");
        let (adv_peripheral_client, _responder) =
            expect_advertise_request(&mut exec, &mut upstream_server);
        expect_stream_pending(&mut exec, &mut advertiser);

        // Upstream server notifies with an invalidly formatted peer (missing all mandatory data).
        let (connect_client, _connect_server) =
            fidl::endpoints::create_request_stream::<ConnectionMarker>().unwrap();
        let connected_fut = adv_peripheral_client.on_connected(le::Peer::EMPTY, connect_client);
        pin_mut!(connected_fut);
        let _ = exec
            .run_until_stalled(&mut connected_fut)
            .expect_pending("waiting for LEAdvertiser response");
        // Handled gracefully and no LE event.
        expect_stream_pending(&mut exec, &mut advertiser);
        let _ = exec.run_until_stalled(&mut connected_fut).expect("receive response");
    }

    #[fuchsia::test]
    async fn stop_advertising_with_no_active_advertisement_is_no_op() {
        let (c, _upstream_server) =
            fidl::endpoints::create_proxy_and_stream::<PeripheralMarker>().unwrap();
        let mut advertiser = LowEnergyAdvertiser::from_proxy(c);
        assert!(!advertiser.stop_advertising().await);
    }
}
