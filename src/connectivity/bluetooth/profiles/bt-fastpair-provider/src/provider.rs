// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_helpers::maybe_stream::MaybeStream;
use fidl_fuchsia_bluetooth_fastpair::{ProviderEnableResponder, ProviderWatcherProxy};
use fidl_fuchsia_bluetooth_gatt2 as gatt;
use fidl_fuchsia_bluetooth_sys::{HostWatcherMarker, PairingMarker, PairingProxy};
use fuchsia_bluetooth::types::PeerId;
use futures::stream::{FusedStream, StreamExt};
use futures::{channel::mpsc, select, FutureExt};
use tracing::{debug, info, trace, warn};

use crate::advertisement::LowEnergyAdvertiser;
use crate::config::Config;
use crate::fidl_client::FastPairConnectionManager;
use crate::gatt_service::{GattRequest, GattService, GattServiceResponder};
use crate::host_watcher::{HostEvent, HostWatcher};
use crate::pairing::{PairingArgs, PairingManager};
use crate::types::keys::aes_from_anti_spoofing_and_public;
use crate::types::packets::{
    decrypt_account_key_request, decrypt_key_based_pairing_request, decrypt_passkey_request,
    decrypt_personalized_name_request, key_based_pairing_response, parse_key_based_pairing_request,
    passkey_response, personalized_name_response, KeyBasedPairingAction, KeyBasedPairingRequest,
};
use crate::types::{AccountKey, AccountKeyList, Error, SharedSecret};

/// The types of FIDL requests that the Provider server can service.
pub enum ServiceRequest {
    /// A request to set the Pairing Delegate.
    Pairing(PairingArgs),
    /// A request to enable the Fast Pair Provider service.
    EnableFastPair { watcher: ProviderWatcherProxy, responder: ProviderEnableResponder },
}

/// State associated with the Fast Pair Provider server.
struct State {
    /// The configuration of the Fast Pair Provider server.
    config: Config,
    /// The local name set by a remote peer. This is None until it is set by the peer.
    personalized_name: Option<String>,
}

/// The toplevel server that manages the current state of the Fast Pair Provider service.
/// Owns the interfaces for interacting with various BT system services.
pub struct Provider {
    /// Current state of the server.
    state: State,
    /// The upstream client that has requested to enable the Fast Pair Provider service. If unset,
    /// the component will not advertise the service over LE.
    upstream: FastPairConnectionManager,
    /// The current set of saved Account Keys.
    account_keys: AccountKeyList,
    /// Manages the Fast Pair advertisement over LE.
    advertiser: LowEnergyAdvertiser,
    /// The published Fast Pair GATT service. The remote peer (Seeker) will interact with this
    /// service to initiate Fast Pair flows.
    gatt: GattService,
    /// Watches for changes in the locally active Bluetooth Host.
    host_watcher: HostWatcher,
    /// Connection to the system pairing service.
    pairing_svc: PairingProxy,
    /// Manages Fast Pair pairing procedures and proxies non-FP requests to the upstream
    /// `sys.Pairing` client.
    pairing: MaybeStream<PairingManager>,
}

impl Provider {
    pub async fn new(config: Config) -> Result<Self, Error> {
        let advertiser = LowEnergyAdvertiser::new()?;
        let gatt = GattService::new(config.clone()).await?;
        let watcher = fuchsia_component::client::connect_to_protocol::<HostWatcherMarker>()?;
        let host_watcher = HostWatcher::new(watcher);
        let pairing_svc = fuchsia_component::client::connect_to_protocol::<PairingMarker>()?;
        Ok(Self {
            state: State { config, personalized_name: None },
            upstream: FastPairConnectionManager::new(),
            account_keys: AccountKeyList::load()?,
            advertiser,
            gatt,
            host_watcher,
            pairing_svc,
            pairing: MaybeStream::default(),
        })
    }

    /// Returns the current local name, if set.
    fn local_name(&self) -> Option<String> {
        // If set, the personalized name is always preferred. Otherwise, defaults to the local
        // name associated with the active host.
        self.state.personalized_name.clone().map_or(self.host_watcher.local_name(), Some)
    }

    async fn advertise(&mut self, discoverable: bool) -> Result<(), Error> {
        if discoverable {
            self.advertiser.advertise_model_id(self.state.config.model_id).await
        } else {
            self.advertiser.advertise_account_keys(&self.account_keys).await
        }
    }

    async fn disable_fast_pair(&mut self) {
        // Stop advertising over LE.
        let _ = self.advertiser.stop_advertising().await;
        // TODO(fxbug.dev/105509): Close ongoing pairing procedures.

        // Reset the upstream connection.
        self.upstream.reset();
    }

    async fn handle_host_watcher_update(&mut self, update: HostEvent) -> Result<(), Error> {
        match update {
            HostEvent::Discoverable(discoverable) | HostEvent::NewActiveHost { discoverable } => {
                // Only advertise if Fast Pair is enabled.
                if self.upstream.is_enabled() {
                    self.advertise(discoverable).await?;
                }
            }
            HostEvent::NotAvailable => {
                // TODO(fxbug.dev/94166): It might make sense to shut down the GATT service.
                let _ = self.advertiser.stop_advertising().await;
            }
        }
        Ok(())
    }

    /// Processes the encrypted key-based pairing `request`.
    /// Implements Steps 1 & 2 in the Pairing Procedure as defined in the GFPS specification. See
    /// https://developers.google.com/nearby/fast-pair/specifications/service/gatt#procedure
    ///
    /// Returns the decrypted request and associated shared secret key if the request was
    /// successfully decrypted.
    /// Returns Error otherwise.
    fn find_key_for_encrypted_request(
        &mut self,
        request: Vec<u8>,
    ) -> Result<(SharedSecret, KeyBasedPairingRequest), Error> {
        // There must be an active local Host to facilitate pairing.
        let discoverable =
            self.host_watcher.pairing_mode().ok_or(Error::internal("No active host"))?;

        let (encrypted_request, remote_public_key) = parse_key_based_pairing_request(request)?;
        let keys_to_try = if let Some(key) = remote_public_key {
            debug!("Trying remote public key");
            // The Public/Private key pairing flow is only accepted if the local Host is
            // discoverable.
            if !discoverable {
                return Err(Error::internal("Active host is not discoverable"));
            }

            let aes_key =
                aes_from_anti_spoofing_and_public(&self.state.config.local_private_key, &key)?;
            vec![aes_key]
        } else {
            debug!("Trying saved Account Keys");
            self.account_keys.keys().map(|k| k.shared_secret()).cloned().collect()
        };

        for key in keys_to_try {
            match decrypt_key_based_pairing_request(&encrypted_request, &key) {
                Ok(request) => {
                    debug!("Found a valid key for pairing");
                    // Refresh the LRU position of the key that successfully decrypted the request
                    // as it will be used for subsequent steps of the pairing procedure. The result
                    // of this operation is ignored as if the `key` is calculated using the
                    // Public/Private keys, then it won't exist in the `account_keys` cache.
                    let _ = self.account_keys.mark_used(&(&key).into());
                    return Ok((key, request));
                }
                Err(e) => {
                    // Errors here are not fatal. We will simply try the next available key.
                    debug!("Key failed to decrypt message: {:?}", e);
                }
            }
        }

        Err(Error::NoAvailableKeys)
    }

    /// Handles a key-based pairing request initiated by the remote `peer_id`.
    fn handle_key_based_pairing_request(
        &mut self,
        peer_id: PeerId,
        encrypted_request: Vec<u8>,
        response: GattServiceResponder,
    ) {
        // If we were able to match the request with an Account Key, notify the GATT characteristic
        // and temporarily save the key. Steps 3-6 in the Pairing Procedure.
        let (key, request) = match self.find_key_for_encrypted_request(encrypted_request) {
            Ok((key, request)) => (key, request),
            Err(e) => {
                info!("Couldn't find a key for the request: {:?}", e);
                response(Err(gatt::Error::WriteRequestRejected));
                return;
            }
        };

        let name = self.local_name();
        // There must be an active local Host and PairingManager to facilitate pairing.
        let local_public_address = if let Some(addr) = self.host_watcher.public_address() {
            addr
        } else {
            warn!("No active local Host available to start key-based pairing");
            response(Err(gatt::Error::UnlikelyError));
            return;
        };
        let pairing = if let Some(p) = self.pairing.inner_mut() {
            p
        } else {
            warn!("No Pairing Manager available to start key-based pairing");
            response(Err(gatt::Error::UnlikelyError));
            return;
        };

        // Some key-based pairing requests require additional steps.
        // TODO(fxbug.dev/96217): Track the salt in `request` to prevent replay attacks.
        match request.action {
            KeyBasedPairingAction::SeekerInitiatesPairing { received_provider_address }
            | KeyBasedPairingAction::PersonalizedNameWrite { received_provider_address } => {
                // We already check the HostWatcher for the existence of an active Host.
                let addresses = self.host_watcher.addresses().expect("active host");
                if !addresses.iter().any(|addr| addr.bytes() == &received_provider_address) {
                    warn!(
                        "Received address ({:?}) doesn't match any local ({:?})",
                        received_provider_address, addresses,
                    );
                    response(Err(gatt::Error::WriteRequestRejected));
                    return;
                }
            }
            KeyBasedPairingAction::ProviderInitiatesPairing { .. } => {
                // TODO(fxbug.dev96222): Use `sys.Access/Pair` to pair to peer.
            }
            KeyBasedPairingAction::RetroactiveWrite { .. } => {
                // TODO(fxbug.dev/99731): Support retroactive account key writes.
            }
        }

        // Notify the GATT service that the write request was successfully processed.
        response(Ok(()));

        let encrypted_response = key_based_pairing_response(&key, local_public_address);
        if let Err(e) = self.gatt.notify_key_based_pairing(peer_id, encrypted_response) {
            warn!("Error notifying Key-based Pairing characteristic: {:?}", e);
            return;
        }

        // Notify the Seeker with the current local host name if known.
        if request.notify_name && name.is_some() {
            debug!(?peer_id, "Notifying local name (name={:?})", name);
            let encrypted_response = personalized_name_response(&key, name.unwrap());
            if let Err(e) = self.gatt.notify_additional_data(peer_id, encrypted_response) {
                warn!("Error notifying Additional Data characteristic: {:?}", e);
            }
        }

        match pairing.new_pairing_procedure(peer_id, key) {
            Ok(_) => info!("Successfully started key-based pairing"),
            Err(e) => warn!("Couldn't start key-based pairing: {:?}", e),
        }
    }

    fn handle_verify_passkey_request(
        &mut self,
        peer_id: PeerId,
        encrypted_request: Vec<u8>,
        response: Box<dyn FnOnce(Result<(), gatt::Error>)>,
    ) {
        // Attempts to decrypt and parse the `encrypted_request`. Returns an encrypted response
        // on success, Error otherwise.
        let verify_fn = || -> Result<Vec<u8>, Error> {
            let pairing_manager =
                self.pairing.inner_mut().ok_or(Error::internal("No pairing manager"))?;
            let key = pairing_manager
                .key_for_procedure(&peer_id)
                .map(Clone::clone)
                .ok_or(Error::internal("No active pairing procedure"))?;

            let seeker_passkey = decrypt_passkey_request(encrypted_request, &key)?;
            // Drive the ongoing pairing procedure to completion. The result of the passkey
            // comparison (e.g whether `seeker_passkey` equals the saved passkey in
            // `pairing_manager`) is not important as we notify the GATT characteristic regardless.
            let passkey = pairing_manager.compare_passkey(peer_id, seeker_passkey)?;
            Ok(passkey_response(&key, passkey))
        };

        match verify_fn() {
            Ok(encrypted_response) => {
                // Notify GATT service that the write has been successfully processed.
                response(Ok(()));
                info!("Validated GATT passkey");

                // Regardless of whether the passkeys are equal or not, we notify the
                // characteristic. See FP Procedure Step 14.
                if let Err(e) = self.gatt.notify_passkey(peer_id, encrypted_response) {
                    warn!("Couldn't notify passkey characteristic: {:?}", e)
                }
            }
            Err(e) => {
                warn!("Error processing GATT Passkey Write: {:?}", e);
                response(Err(gatt::Error::WriteRequestRejected));
                if let Some(p) = self.pairing.inner_mut() {
                    let _ = p.cancel_pairing_procedure(&peer_id);
                }
            }
        }
    }

    fn handle_write_account_key_request(
        &mut self,
        peer_id: PeerId,
        encrypted_request: Vec<u8>,
        response: Box<dyn FnOnce(Result<(), gatt::Error>)>,
    ) {
        // Attempts to decrypt and parse the the `encrypted_request`. Returns an Account Key on
        // success, Error otherwise.
        let verify_fn = || -> Result<AccountKey, Error> {
            let pairing_manager =
                self.pairing.inner_mut().ok_or(Error::internal("No pairing manager"))?;
            let key = pairing_manager
                .key_for_procedure(&peer_id)
                .ok_or(Error::internal("No active pairing procedure"))?;

            let account_key = decrypt_account_key_request(encrypted_request, key)?;
            // The key-based pairing procedure is officially complete. The shared secret is still
            // persisted because the peer may request to set a personalized device name.
            pairing_manager.account_key_write(peer_id)?;
            Ok(account_key)
        };

        match verify_fn() {
            Ok(key) => {
                // Notify GATT service that the write has been processed.
                response(Ok(()));
                self.account_keys.save(key);
                info!("Successfully saved Account Key");
            }
            Err(e) => {
                warn!("Error processing GATT Account Key Write: {:?}", e);
                response(Err(gatt::Error::WriteRequestRejected));
                if let Some(p) = self.pairing.inner_mut() {
                    let _ = p.cancel_pairing_procedure(&peer_id);
                }
            }
        }
    }

    fn handle_additional_data_request(
        &mut self,
        peer_id: PeerId,
        request: Vec<u8>,
        response: Box<dyn FnOnce(Result<(), gatt::Error>)>,
    ) {
        // The request can only be decrypted if there is a procedure in progress.
        let parse_fn = || -> Result<String, Error> {
            let pairing_manager =
                self.pairing.inner_mut().ok_or(Error::internal("No pairing manager"))?;
            let key = pairing_manager
                .key_for_procedure(&peer_id)
                .map(Clone::clone)
                .ok_or(Error::internal("No active pairing procedure"))?;

            let personalized_name = decrypt_personalized_name_request(&key, request)?;
            // All mandatory and optional steps are complete. The shared secret is no longer valid
            // and the procedure can be cleaned up.
            pairing_manager.complete_pairing_procedure(peer_id)?;
            Ok(personalized_name)
        };
        let result = match parse_fn() {
            Ok(name) => {
                debug!(?peer_id, "Received request to save personalized name: {}", name);
                self.state.personalized_name = Some(name);
                Ok(())
            }
            Err(e) => {
                warn!(?peer_id, "Couldn't process additional data request: {:?}", e);
                Err(gatt::Error::UnlikelyError)
            }
        };
        response(result);
    }

    fn handle_gatt_update(&mut self, update: GattRequest) {
        // GATT updates will only be processed if Fast Pair is enabled. Otherwise, reject the
        // update.
        if !self.upstream.is_enabled() {
            trace!("Received GATT request while Fast Pair is disabled. Rejecting.");
            update.responder()(Err(gatt::Error::WriteRequestRejected));
            return;
        }

        match update {
            GattRequest::KeyBasedPairing { peer_id, encrypted_request, response } => {
                self.handle_key_based_pairing_request(peer_id, encrypted_request, response);
            }
            GattRequest::VerifyPasskey { peer_id, encrypted_passkey, response } => {
                self.handle_verify_passkey_request(peer_id, encrypted_passkey, response);
            }
            GattRequest::WriteAccountKey { peer_id, encrypted_account_key, response } => {
                self.handle_write_account_key_request(peer_id, encrypted_account_key, response);
            }
            GattRequest::AdditionalData { peer_id, encrypted_data, response } => {
                self.handle_additional_data_request(peer_id, encrypted_data, response);
            }
        }
    }

    async fn handle_fidl_request(&mut self, request: ServiceRequest) -> Result<(), Error> {
        match request {
            ServiceRequest::Pairing(client) => {
                if self.pairing.inner_mut().map_or(true, |p| p.is_terminated()) {
                    self.pairing.set(PairingManager::new(self.pairing_svc.clone(), client)?);
                } else {
                    warn!("Pairing Delegate is already active, ignoring..");
                }
            }
            ServiceRequest::EnableFastPair { watcher, responder } => {
                match self.upstream.set(watcher) {
                    Ok(()) => {
                        let _ = responder.send(&mut Ok(()));
                        if let Some(discoverable) = self.host_watcher.pairing_mode() {
                            self.advertise(discoverable).await?;
                        }
                    }
                    Err(e) => {
                        info!("Couldn't enable Fast Pair: {:?}", e);
                        let _ = responder
                            .send(&mut Err(fuchsia_zircon::Status::ALREADY_BOUND.into_raw()));
                    }
                }
            }
        }
        Ok(())
    }

    /// Run the Fast Pair Provider service to completion.
    /// Terminates if an irrecoverable error is encountered, or all Fast Pair related resources
    /// have been exhausted.
    pub async fn run(mut self, mut requests: mpsc::Receiver<ServiceRequest>) -> Result<(), Error> {
        loop {
            let mut upstream_client_closed_fut = self.upstream.on_upstream_client_closed().fuse();
            select! {
                // It's OK if the advertisement terminates for any reason. We use `select_next_some`
                // to ignore cases where the stream is exhausted. It can always be set again if
                // needed.
                advertise_update = self.advertiser.select_next_some() => {
                    debug!("Low energy event: {:?}", advertise_update);
                }
                gatt_update = self.gatt.next() => {
                    debug!("GATT event: {:?}", gatt_update);
                    // Unexpected termination of the GATT service is a fatal error.
                    match gatt_update {
                        Some(update) => self.handle_gatt_update(update),
                        None => return Err(Error::internal("GATT Server unexpectedly terminated")),
                    }
                }
                watcher_update = self.host_watcher.next() => {
                    debug!("HostWatcher event: {:?}", watcher_update);
                    // Unexpected termination of the Host Watcher is a fatal error.
                    match watcher_update {
                        Some(update) => self.handle_host_watcher_update(update).await?,
                        None => return Err(Error::internal("HostWatcher unexpectedly terminated")),
                    }
                }
                pairing_update = self.pairing.next() => {
                    debug!("Pairing event: {:?}", pairing_update);
                    match pairing_update {
                        None => {
                            // Either upstream `sys.Pairing` client or downstream host server
                            // terminated the connection.
                            self.pairing = Default::default();
                        }
                        Some(id) => {
                            // Pairing completed for peer.
                            info!("Fast Pair pairing completed with peer: {:?}", id);
                            self.upstream.notify_pairing_complete(id);
                        }
                    }
                }
                service_request = requests.next() => {
                    match service_request {
                        None => return Err(Error::internal("FIDL service handler unexpectedly terminated")),
                        Some(request) => self.handle_fidl_request(request).await?,
                    }
                }
                _ = upstream_client_closed_fut => {
                    info!("Upstream client disabled Fast Pair");
                    self.disable_fast_pair().await;
                }
                complete => {
                    break;
                }
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use async_test_helpers::run_while;
    use async_utils::PollExt;
    use fidl::endpoints::{create_proxy_and_stream, ControlHandle, Proxy, RequestStream};
    use fidl_fuchsia_bluetooth_gatt2::{
        Handle, LocalServiceProxy, LocalServiceWriteValueRequest as WriteValueRequest,
        ValueChangedParameters,
    };
    use fidl_fuchsia_bluetooth_le::{PeripheralMarker, PeripheralRequestStream};
    use fidl_fuchsia_bluetooth_sys::{
        HostWatcherRequestStream, InputCapability, OutputCapability, PairingDelegateMarker,
    };
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::{Address, HostId};
    use futures::{pin_mut, FutureExt, SinkExt};
    use std::convert::{TryFrom, TryInto};

    use crate::fidl_client::tests::MockUpstreamClient;
    use crate::gatt_service::tests::setup_gatt_service;
    use crate::gatt_service::{
        ACCOUNT_KEY_CHARACTERISTIC_HANDLE, ADDITIONAL_DATA_CHARACTERISTIC_HANDLE,
        KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE, PASSKEY_CHARACTERISTIC_HANDLE,
    };
    use crate::host_watcher::tests::example_host;
    use crate::pairing::tests::MockPairing;
    use crate::types::keys::tests::{encrypt_message, encrypt_message_include_public_key};
    use crate::types::packets::tests::{
        key_based_pairing_request, ACCOUNT_KEY_REQUEST, DEVICE_ACTION_PERSONALIZED_NAME_REQUEST,
        PASSKEY_REQUEST,
    };
    use crate::types::tests::expect_keys_at_path;
    use crate::types::{keys, ModelId};

    async fn setup_provider() -> (
        Provider,
        PeripheralRequestStream,
        LocalServiceProxy,
        HostWatcherRequestStream,
        MockPairing,
        MockUpstreamClient,
    ) {
        let state = State { config: Config::example_config(), personalized_name: None };

        let (peripheral_proxy, peripheral_server) =
            create_proxy_and_stream::<PeripheralMarker>().unwrap();
        let advertiser = LowEnergyAdvertiser::from_proxy(peripheral_proxy);

        let (gatt, local_service_proxy) = setup_gatt_service().await;

        let (watcher_client, watcher_server) =
            create_proxy_and_stream::<HostWatcherMarker>().unwrap();
        let host_watcher = HostWatcher::new(watcher_client);

        let (pairing, mock_pairing) = MockPairing::new_with_manager().await;

        // By default, enable Fast Pair.
        let (mock_upstream, c) = MockUpstreamClient::new();
        let upstream = FastPairConnectionManager::new_with_upstream(c);

        let this = Provider {
            state,
            upstream,
            account_keys: AccountKeyList::with_capacity_and_keys(10, vec![]),
            advertiser,
            gatt,
            host_watcher,
            pairing_svc: mock_pairing.pairing_svc.clone(),
            pairing: Some(pairing).into(),
        };

        (this, peripheral_server, local_service_proxy, watcher_server, mock_pairing, mock_upstream)
    }

    #[fuchsia::test]
    async fn terminates_if_host_watcher_closes() {
        let (provider, _peripheral, _gatt, host_watcher_stream, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        let (_sender, receiver) = mpsc::channel(0);
        let provider_fut = provider.run(receiver);
        pin_mut!(provider_fut);

        // Upstream `bt-gap` can no longer service HostWatcher requests.
        host_watcher_stream.control_handle().shutdown();
        drop(host_watcher_stream);

        let result = provider_fut.await;
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[fuchsia::test]
    async fn terminates_if_gatt_closes() {
        let (provider, _peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        let (_sender, receiver) = mpsc::channel(0);
        let provider_fut = provider.run(receiver);
        pin_mut!(provider_fut);

        // Upstream bt-host no longer can support advertising the GATT service.
        drop(gatt);

        let result = provider_fut.await;
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[fuchsia::test]
    async fn terminates_if_fidl_request_handler_closes() {
        let (provider, _peripheral, _gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        let (sender, receiver) = mpsc::channel(0);
        let provider_fut = provider.run(receiver);
        pin_mut!(provider_fut);

        drop(sender);
        let result = provider_fut.await;
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[fuchsia::test]
    async fn advertisement_changes_upon_host_discoverable_change() {
        let (provider, mut le_peripheral, _gatt, mut host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        let (_sender, _provider_server) = server_task(provider);

        // Expect the Provider server to watch active hosts.
        let watch_request = host_watcher.select_next_some().await.expect("fidl request");
        let watch_responder = watch_request.into_watch().expect("HostWatcher::Watch request");
        // A new discoverable local Bluetooth host appears on the system.
        let discoverable =
            vec![example_host(HostId(1), /* active= */ true, /* discoverable= */ true)];
        let _ = watch_responder.send(&mut discoverable.into_iter()).unwrap();

        // Provider server should recognize this and attempt to advertise Model ID over LE.
        let advertise_request = le_peripheral.select_next_some().await.expect("fidl request");
        let (params, adv_client, responder) =
            advertise_request.into_advertise().expect("advertise request");
        let service_data = &params.data.unwrap().service_data.unwrap()[0].data;
        let expected_data: [u8; 3] = ModelId::try_from(1).expect("valid ID").into();
        assert_eq!(service_data, &expected_data.to_vec());

        // Host becomes not discoverable.
        let watch_request = host_watcher.select_next_some().await.expect("fidl request");
        let watch_responder = watch_request.into_watch().expect("HostWatcher::Watch request");
        let non_discoverable =
            vec![example_host(HostId(1), /* active= */ true, /* discoverable= */ false)];
        let _ = watch_responder.send(&mut non_discoverable.into_iter()).unwrap();

        // Provider server should cancel the existing advertisement and attempt to advertise
        // Account Keys.
        // Simulate upstream server detecting this.
        let adv_client = adv_client.into_proxy().unwrap();
        let _ = adv_client.on_closed().await;
        let _ = responder.send(&mut Ok(())).unwrap();

        // Upstream LE server should receive the new LE advertisement. Since there are no Account
        // Keys, the expected service data is minimal.
        let advertise_request = le_peripheral.select_next_some().await.expect("fidl request");
        let (params, adv_client, responder) =
            advertise_request.into_advertise().expect("advertise request");
        let service_data = &params.data.unwrap().service_data.unwrap()[0].data;
        assert_eq!(service_data, &[0, 0]);

        // No more active hosts.
        let watch_request = host_watcher.select_next_some().await.expect("fidl request");
        let watch_responder = watch_request.into_watch().expect("HostWatcher::Watch request");
        let not_active =
            vec![example_host(HostId(1), /* active= */ false, /* discoverable= */ false)];
        let _ = watch_responder.send(&mut not_active.into_iter()).unwrap();

        // Provider server should cancel the existing advertisement.
        let adv_client = adv_client.into_proxy().unwrap();
        let _ = adv_client.on_closed().await;
        let _ = responder.send(&mut Ok(())).unwrap();
    }

    #[fuchsia::test]
    async fn le_advertisement_is_stopped_when_fast_pair_disabled() {
        let (provider, mut le_peripheral, _gatt, mut host_watcher, _mock_pairing, mock_upstream) =
            setup_provider().await;
        let (_sender, _provider_server) = server_task(provider);

        // Expect the Provider server to watch active hosts.
        let watch_request = host_watcher.select_next_some().await.expect("fidl request");
        let watch_responder = watch_request.into_watch().expect("HostWatcher::Watch request");
        // Set a local BT host that is discoverable.
        let discoverable =
            vec![example_host(HostId(1), /* active= */ true, /* discoverable= */ true)];
        let _ = watch_responder.send(&mut discoverable.into_iter()).unwrap();
        // Provider server should recognize this and attempt to advertise Model ID over LE.
        let advertise_request = le_peripheral.select_next_some().await.expect("fidl request");
        let (params, adv_client, _responder) =
            advertise_request.into_advertise().expect("advertise request");
        let service_data = &params.data.unwrap().service_data.unwrap()[0].data;
        let expected_data: [u8; 3] = ModelId::try_from(1).expect("valid ID").into();
        assert_eq!(service_data, &expected_data.to_vec());

        // Advertisement should be active - e.g. not closed.
        let advertisement = adv_client.into_proxy().unwrap();
        let advertisement_fut = advertisement.on_closed();
        assert_matches!(advertisement_fut.now_or_never(), None);

        // Upstream Fast Pair client disables the service - should no longer be advertising over LE.
        drop(mock_upstream);
        let advertisement_fut = advertisement.on_closed();
        assert_matches!(advertisement_fut.await, Ok(_));
    }

    #[fuchsia::test]
    async fn subsequent_enabling_of_fast_pair_is_rejected() {
        let (provider, _le_peripheral, _gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        let (mut sender, _provider_server) = server_task(provider);

        // By default (see `setup_provider`), there is an active upstream client that has enabled
        // Fast Pair. A subsequent request to enable the service is an Error and should be handled
        // gracefully by the Provider server.
        let (request_fut, service_request, _mock_upstream1) =
            MockUpstreamClient::make_enable_request().await;
        let () = sender.send(service_request).await.expect("component main loop active");

        // Should be rejected.
        let result = request_fut.await.expect("fidl response");
        assert_eq!(result, Err(fuchsia_zircon::Status::ALREADY_BOUND.into_raw()));
    }

    #[fuchsia::test]
    fn subsequent_enabling_when_previous_closed_is_ok() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let setup_fut = setup_provider();
        pin_mut!(setup_fut);
        let (provider, mut le_peripheral, _gatt, mut host_watcher, _mock_pairing, _mock_upstream) =
            exec.run_singlethreaded(&mut setup_fut);

        let (mut sender, receiver) = mpsc::channel(0);
        let server_fut = provider.run(receiver);
        pin_mut!(server_fut);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // Existing client disables the service.
        drop(_mock_upstream);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // Simulate an active host so that we can verify the LE advertisement gets set.
        let watch_request_fut = host_watcher.select_next_some();
        pin_mut!(watch_request_fut);
        let watch_request = exec
            .run_until_stalled(&mut watch_request_fut)
            .expect("watch request")
            .expect("request is OK");
        let watch_responder = watch_request.into_watch().expect("HostWatcher::Watch request");
        let discoverable =
            vec![example_host(HostId(1), /* active= */ true, /* discoverable= */ true)];
        let _ = watch_responder.send(&mut discoverable.into_iter()).unwrap();
        // Even though there is an active Host, there should be no LE advertisement as Fast Pair is
        // currently disabled.
        let _ = exec
            .run_until_stalled(&mut le_peripheral.select_next_some())
            .expect_pending("no advertise request");

        // A new client can request to enable the service.
        let enable_request_fut = MockUpstreamClient::make_enable_request();
        pin_mut!(enable_request_fut);
        let (request_fut, service_request, _mock_upstream1) =
            exec.run_singlethreaded(enable_request_fut);

        let send_fut = sender.send(service_request);
        let (send_result, server_fut) = run_while(&mut exec, server_fut, send_fut);
        assert_matches!(send_result, Ok(_));

        // Expect the FIDL `request_fut` to be successful.
        let (request_result, _server_fut) = run_while(&mut exec, server_fut, request_fut);
        assert_matches!(request_result, Ok(_));
        // Expect the LE advertisement for the Fast Pair service.
        let _advertise_request = exec
            .run_until_stalled(&mut le_peripheral.select_next_some())
            .expect("advertise request received")
            .expect("request is OK");
    }

    #[fuchsia::test]
    fn gatt_update_when_disabled_is_rejected() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let setup_fut = setup_provider();
        pin_mut!(setup_fut);
        let (provider, _le_peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            exec.run_singlethreaded(&mut setup_fut);

        let (_sender, receiver) = mpsc::channel(0);
        let server_fut = provider.run(receiver);
        pin_mut!(server_fut);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // Existing client disables the service.
        drop(_mock_upstream);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // A Key-based pairing GATT request should be rejected.
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message_include_public_key(&key_based_pairing_request(0x00)),
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        );
        let ((), _server_fut) = run_while(&mut exec, server_fut, write_fut);
    }

    /// Builds the Task associated with a running Provider server.
    /// Returns a sender for FIDL service requests and a Task representing the server.
    fn server_task(provider: Provider) -> (mpsc::Sender<ServiceRequest>, fasync::Task<()>) {
        let (sender, receiver) = mpsc::channel(0);
        let provider_fut = provider.run(receiver);
        let task = fasync::Task::local(async move {
            let result = provider_fut.await;
            panic!("Provider server unexpectedly finished with result: {:?}", result);
        });
        (sender, task)
    }

    const PEER_ID: PeerId = PeerId(123);

    #[track_caller]
    async fn gatt_write_results_in_expected_notification(
        gatt: &LocalServiceProxy,
        handle: Handle,
        encrypted_buf: Vec<u8>,
        expected_result: Result<(), gatt::Error>,
        expect_item: bool,
    ) {
        let result = gatt
            .write_value(WriteValueRequest {
                peer_id: Some(PEER_ID.into()),
                handle: Some(handle),
                offset: Some(0),
                value: Some(encrypted_buf),
                ..WriteValueRequest::EMPTY
            })
            .await
            .expect("valid FIDL request");
        assert_eq!(result, expected_result);

        let mut stream = gatt.take_event_stream();
        if expect_item {
            let ValueChangedParameters { peer_ids, handle: received_handle, .. } = stream
                .select_next_some()
                .await
                .expect("valid event")
                .into_on_notify_value()
                .expect("notification");
            assert_eq!(peer_ids, Some(vec![PEER_ID.into()]));
            assert_eq!(received_handle, Some(handle));
        } else {
            assert_matches!(stream.next().now_or_never(), None);
        }
    }

    /// This test is a large integration-style test that verifies the key-based pairing procedure.
    #[fuchsia::test]
    fn key_based_pairing_procedure() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let setup_fut = setup_provider();
        pin_mut!(setup_fut);
        let (
            mut provider,
            _le_peripheral,
            gatt,
            _host_watcher,
            mut mock_pairing,
            mut mock_upstream,
        ) = exec.run_singlethreaded(&mut setup_fut);

        // To avoid a bunch of unnecessary boilerplate involving the `host_watcher`, set the active
        // host with a known address.
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ true)
                .try_into()
                .unwrap(),
        );
        let (_sender, receiver) = mpsc::channel(0);
        let server_fut = provider.run(receiver);
        pin_mut!(server_fut);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // Before starting the pairing procedure, there should be no saved Account Keys.
        expect_keys_at_path(AccountKeyList::TEST_PERSISTED_ACCOUNT_KEYS_FILEPATH, vec![]);

        // Initiating a Key-based pairing request should succeed. The buffer is encrypted by the key
        // defined in the GFPS.
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message_include_public_key(&key_based_pairing_request(0x00)),
            Ok(()),
            /* expect_item= */ true,
        );
        let (_, server_fut) = run_while(&mut exec, server_fut, write_fut);

        // We expect Fast Pair to take ownership of the delegate - new SetPairingDelegate request.
        let expect_fut = mock_pairing.expect_set_pairing_delegate();
        let (_, mut server_fut) = run_while(&mut exec, server_fut, expect_fut);

        // Peer requests to pair - drive the future associated with the request and the server to
        // process it.
        let pairing_fut = mock_pairing.make_pairing_request(PEER_ID, 0x123456);
        pin_mut!(pairing_fut);
        let _ = exec.run_until_stalled(&mut pairing_fut).expect_pending("waiting for response");
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("main loop still active");
        // The request shouldn't resolve yet as we are waiting for passkey verification.
        let _ = exec.run_until_stalled(&mut pairing_fut).expect_pending("waiting for response");

        // Expect the peer to then send the passkey over GATT.
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            PASSKEY_CHARACTERISTIC_HANDLE,
            encrypt_message(&PASSKEY_REQUEST),
            Ok(()),
            /* expect_item= */ true,
        );
        let (_, server_fut) = run_while(&mut exec, server_fut, write_fut);

        // The Fast Pair pairing procedure is not complete but the PairingDelegate request has been
        // responded to.
        let (pairing_result, mut server_fut) = run_while(&mut exec, server_fut, pairing_fut);
        assert_eq!(pairing_result.unwrap(), (true, 0x123456));

        // Peer responds positively to successfully finish pairing.
        let _ = mock_pairing
            .downstream_delegate_client
            .on_pairing_complete(&mut PEER_ID.into(), true)
            .expect("valid FIDL request");
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("main loop still active");

        // After pairing succeeds, the peer will request to write an Account Key.
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            ACCOUNT_KEY_CHARACTERISTIC_HANDLE,
            encrypt_message(&ACCOUNT_KEY_REQUEST),
            Ok(()),
            /* expect_item= */ false,
        );
        let (_, server_fut) = run_while(&mut exec, server_fut, write_fut);
        // Account Key should be saved to persistent storage.
        expect_keys_at_path(
            AccountKeyList::TEST_PERSISTED_ACCOUNT_KEYS_FILEPATH,
            vec![AccountKey::new(ACCOUNT_KEY_REQUEST)],
        );

        // The peer can optionally request to set a personalized name.
        let new_name = "myfuchsia123".to_string();
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            ADDITIONAL_DATA_CHARACTERISTIC_HANDLE,
            personalized_name_response(&keys::tests::example_aes_key(), new_name.clone()),
            Ok(()),
            /* expect_item= */ false,
        );
        let (_, server_fut) = run_while(&mut exec, server_fut, write_fut);

        // `flags` = notify name. The updated name should be returned.
        let encrypted_buf = encrypt_message_include_public_key(&key_based_pairing_request(0x20));
        let name_request_fut = make_personalized_name_request(&gatt, PeerId(999), encrypted_buf);
        let (returned_name, _server_fut) = run_while(&mut exec, server_fut, name_request_fut);
        assert_eq!(returned_name, new_name);

        // Upstream client should be notified that Fast Pair pairing has successfully completed.
        let pairing_complete_fut = mock_upstream.expect_on_pairing_complete(PEER_ID);
        pin_mut!(pairing_complete_fut);
        let () = exec.run_until_stalled(&mut pairing_complete_fut).expect("should resolve");
    }

    #[fuchsia::test]
    async fn data_characteristic_notified_after_kbp_request() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;

        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ true)
                .try_into()
                .unwrap(),
        );
        let (_sender, _provider_server) = server_task(provider);

        // Initiating a key-based pairing request should succeed. `flags` = notify name
        // We expect the standard KBP GATT notification as well as the additional data notification.
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message_include_public_key(&key_based_pairing_request(0x20)),
            Ok(()),
            /* expect_item= */ true,
        )
        .await;

        // Expect another GATT notification.
        let mut gatt_stream = gatt.take_event_stream();
        let ValueChangedParameters { handle, .. } = gatt_stream
            .select_next_some()
            .await
            .expect("valid event")
            .into_on_notify_value()
            .expect("notification");
        assert_eq!(handle, Some(ADDITIONAL_DATA_CHARACTERISTIC_HANDLE));
    }

    #[fuchsia::test]
    async fn public_key_pairing_request_with_no_host() {
        let (provider, _le_peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        let (_sender, _provider_server) = server_task(provider);

        // Initiating a valid key-based pairing request should succeed and be handled gracefully.
        // Because there's no active host, we don't expect any subsequent GATT notification.
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message_include_public_key(&key_based_pairing_request(0x00)),
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    async fn public_key_pairing_request_with_ble_address_succeeds() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;

        // Give the active host an additional random BLE address.
        let mut example_host =
            example_host(HostId(1), /* active= */ true, /* discoverable= */ true);
        let ble_address = Address::Random([2, 3, 2, 3, 2, 3]);
        example_host.addresses.as_mut().unwrap().push(ble_address.into());
        provider.host_watcher.set_active_host(example_host.try_into().unwrap());
        let (_sender, _provider_server) = server_task(provider);

        // The provided Provider address is the known BLE address - the request should succeed.

        // Bytes 2-7 correspond to the address (in BE).
        let mut request_with_ble_address = key_based_pairing_request(0x00);
        let mut address_bytes = ble_address.bytes().clone();
        address_bytes.reverse();
        request_with_ble_address[2..8].copy_from_slice(&address_bytes[..]);
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message_include_public_key(&request_with_ble_address),
            Ok(()),
            /* expect_item= */ true,
        )
        .await;
    }

    #[fuchsia::test]
    async fn public_key_pairing_request_with_invalid_provider_address_fails() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ true)
                .try_into()
                .unwrap(),
        );
        let (_sender, _provider_server) = server_task(provider);

        // The provided Provider address is incorrect - therefore the key based pairing request
        // should be rejected.

        // Bytes 2-7 correspond to the address.
        let mut request_with_invalid_address = key_based_pairing_request(0x00);
        request_with_invalid_address[4] = 0xff;
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message_include_public_key(&request_with_invalid_address),
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    async fn public_key_pairing_request_with_not_discoverable_host() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        let (_sender, _provider_server) = server_task(provider);

        // Initiating a key-based pairing request with public key should succeed and be handled
        // gracefully. Because the local host is not discoverable, we don't expect a subsequent GATT
        // notification.
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message_include_public_key(&key_based_pairing_request(0x00)),
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    async fn pairing_request_no_saved_keys() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        let (_sender, _provider_server) = server_task(provider);

        // Initiating a key-based pairing request should be handled gracefully. Pairing should not
        // continue because the `encrypted_buf` doesn't contain the peer's public key and there are
        // no locally saved Account Keys.
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message(&key_based_pairing_request(0x00)),
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    async fn pairing_request_with_saved_key_notifies_gatt() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        // A active host that is not discoverable - expect to go through the subsequent pairing flow
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        // Two keys - one key should work, other is random.
        provider.account_keys = AccountKeyList::with_capacity_and_keys(
            10,
            vec![
                AccountKey::new([2; 16]),
                AccountKey::new(keys::tests::example_aes_key().as_bytes().clone()),
            ],
        );
        let (_sender, _provider_server) = server_task(provider);

        // Initiating a key-based pairing request should be handled gracefully. Because there are no
        // saved Account Keys, pairing should not continue.
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message(&key_based_pairing_request(0x00)),
            Ok(()),
            /* expect_item= */ true,
        )
        .await;
    }

    #[fuchsia::test]
    async fn passkey_write_with_no_active_pairing_procedure() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        let (_sender, _provider_server) = server_task(provider);

        // Initiating a passkey write request should be handled gracefully. Because there is no
        // active pairing procedure (e.g a key-based pairing write was never received), pairing
        // should not continue and the GATT request should result in Error.
        gatt_write_results_in_expected_notification(
            &gatt,
            PASSKEY_CHARACTERISTIC_HANDLE,
            encrypt_message(&PASSKEY_REQUEST),
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    fn passkey_write_with_no_pairing_manager_is_error() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let setup_fut = setup_provider();
        pin_mut!(setup_fut);
        let (mut provider, _le_peripheral, gatt, _host_watcher, mut mock_pairing, _mock_upstream) =
            exec.run_singlethreaded(&mut setup_fut);

        // Set the active host with a known address.
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ true)
                .try_into()
                .unwrap(),
        );
        let (_sender, receiver) = mpsc::channel(0);
        let server_fut = provider.run(receiver);
        pin_mut!(server_fut);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // Key-based pairing begins.
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message_include_public_key(&key_based_pairing_request(0x00)),
            Ok(()),
            /* expect_item= */ true,
        );
        let (_, server_fut) = run_while(&mut exec, server_fut, write_fut);

        // We expect Fast Pair to take ownership of the delegate.
        let expect_fut = mock_pairing.expect_set_pairing_delegate();
        let (_, mut server_fut) = run_while(&mut exec, server_fut, expect_fut);

        // Before the next passkey-related request comes through, the PairingManager disconnects.
        mock_pairing.upstream_delegate_server.control_handle().shutdown();
        drop(mock_pairing.upstream_delegate_server);
        // Running the main loop should process this disconnection.
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("main loop still active");

        // Because there is no PairingManager, a GATT passkey write request should be rejected.
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            PASSKEY_CHARACTERISTIC_HANDLE,
            encrypt_message(&PASSKEY_REQUEST),
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        );
        let (_, _server_fut) = run_while(&mut exec, server_fut, write_fut);
    }

    // TODO(fxbug.dev/102963): This test will be obsolete if the PairingManager is resilient to
    // ordering of events.
    #[fuchsia::test]
    fn account_key_write_before_pairing_complete_is_error() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let setup_fut = setup_provider();
        pin_mut!(setup_fut);
        let (mut provider, _le_peripheral, gatt, _host_watcher, mut mock_pairing, _mock_upstream) =
            exec.run_singlethreaded(&mut setup_fut);

        // To avoid a bunch of unnecessary boilerplate involving the `host_watcher`, set the active
        // host with a known address.
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ true)
                .try_into()
                .unwrap(),
        );
        let (_sender, receiver) = mpsc::channel(0);
        let server_fut = provider.run(receiver);
        pin_mut!(server_fut);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // Initiating a Key-based pairing request should succeed. The buffer is encrypted by the key
        // defined in the GFPS.
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message_include_public_key(&key_based_pairing_request(0x00)),
            Ok(()),
            /* expect_item= */ true,
        );
        let (_, server_fut) = run_while(&mut exec, server_fut, write_fut);

        // We expect Fast Pair to take ownership of the delegate - new SetPairingDelegate request.
        let expect_fut = mock_pairing.expect_set_pairing_delegate();
        let (_, mut server_fut) = run_while(&mut exec, server_fut, expect_fut);

        // Peer requests to pair - drive the future associated with the request and the server to
        // process it.
        let pairing_fut = mock_pairing.make_pairing_request(PEER_ID, 0x123456);
        pin_mut!(pairing_fut);
        let _ = exec.run_until_stalled(&mut pairing_fut).expect_pending("waiting for response");
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("main loop still active");
        let _ = exec.run_until_stalled(&mut pairing_fut).expect_pending("waiting for response");

        // Expect the peer to then send the passkey over GATT.
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            PASSKEY_CHARACTERISTIC_HANDLE,
            encrypt_message(&PASSKEY_REQUEST),
            Ok(()),
            /* expect_item= */ true,
        );
        let (_, server_fut) = run_while(&mut exec, server_fut, write_fut);

        // The Fast Pair pairing procedure is not complete but the PairingDelegate request has been
        // responded to.
        let (pairing_result, server_fut) = run_while(&mut exec, server_fut, pairing_fut);
        assert_eq!(pairing_result.unwrap(), (true, 0x123456));

        // The peer requests to write the Account Key before pairing is complete. This should be
        // rejected.
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            ACCOUNT_KEY_CHARACTERISTIC_HANDLE,
            encrypt_message(&ACCOUNT_KEY_REQUEST),
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        );
        let (_, _server_fut) = run_while(&mut exec, server_fut, write_fut);
    }

    /// Makes a key-based pairing request with the notify personalized name flag set.
    /// Returns the name that was received on the additional data characteristic.
    #[track_caller]
    async fn make_personalized_name_request(
        gatt: &LocalServiceProxy,
        id: PeerId,
        encrypted_buf: Vec<u8>,
    ) -> String {
        let () = gatt
            .write_value(WriteValueRequest {
                peer_id: Some(id.into()),
                handle: Some(KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE),
                offset: Some(0),
                value: Some(encrypted_buf),
                ..WriteValueRequest::EMPTY
            })
            .await
            .expect("valid FIDL request")
            .expect("GATT write is OK");

        // We expect two notifications:
        // 1) Key-based pairing characteristic acknowledging success
        // 2) Additional data characteristic with the current local name.
        let mut stream = gatt.take_event_stream();
        let ValueChangedParameters { handle, .. } = stream
            .select_next_some()
            .await
            .expect("valid event")
            .into_on_notify_value()
            .expect("notification");
        assert_eq!(handle, Some(KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE));

        let ValueChangedParameters { handle, value, .. } = stream
            .select_next_some()
            .await
            .expect("valid event")
            .into_on_notify_value()
            .expect("notification");
        assert_eq!(handle, Some(ADDITIONAL_DATA_CHARACTERISTIC_HANDLE));
        let value = value.expect("encrypted notification");
        decrypt_personalized_name_request(&keys::tests::example_aes_key(), value)
            .expect("notification correctly formatted")
    }

    /// Tests the personalized name flow that can be initiated via the key-based pairing
    /// characteristic. This flow bypasses the main pairing procedure.
    #[fuchsia::test]
    async fn personalized_name_write() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;

        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ true)
                .try_into()
                .unwrap(),
        );
        let (_sender, _provider_server) = server_task(provider);

        // First peer wants to know the personalized name. `flags` = notify name
        let other_id = PeerId(1001);
        let encrypted_buf = encrypt_message_include_public_key(&key_based_pairing_request(0x20));
        let current_name = make_personalized_name_request(&gatt, other_id, encrypted_buf).await;
        // Because the personalized name has not been set yet, we expect the returned name to be the
        // default name associated with the local host - set in `example_host()`.
        assert_eq!(current_name, "fuchsia123".to_string());

        // A different peer wants to set the personalized name by making a Device Action request
        // via the key-based pairing characteristic.
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypt_message_include_public_key(&DEVICE_ACTION_PERSONALIZED_NAME_REQUEST),
            Ok(()),
            /* expect_item= */ true,
        )
        .await;
        // It then sets the name by writing to the additional data characteristic.
        gatt_write_results_in_expected_notification(
            &gatt,
            ADDITIONAL_DATA_CHARACTERISTIC_HANDLE,
            personalized_name_response(&keys::tests::example_aes_key(), "myfuchsia".to_string()),
            Ok(()),
            /* expect_item= */ false,
        )
        .await;

        // Trying to get the name should return the updated name. `flags` = notify name.
        let random_id = PeerId(999);
        let encrypted_buf = encrypt_message_include_public_key(&key_based_pairing_request(0x20));
        let current_name = make_personalized_name_request(&gatt, random_id, encrypted_buf).await;
        // The returned name should be the personalized name set by the previous peer.
        assert_eq!(current_name, "myfuchsia".to_string());
    }

    #[fuchsia::test]
    async fn pairing_fidl_request_ignored_when_already_set() {
        // By default, the created `provider` server will have an active PairingManager.
        let (provider, _le_peripheral, _gatt, _host_watcher, _mock_pairing, _mock_upstream) =
            setup_provider().await;
        let (mut sender, _provider_server) = server_task(provider);

        // Simulate a new FIDL client trying to set the Pairing Delegate.
        let (delegate, mut delegate_server) =
            create_proxy_and_stream::<PairingDelegateMarker>().unwrap();
        let client =
            PairingArgs { input: InputCapability::None, output: OutputCapability::None, delegate };
        let _ = sender
            .send(ServiceRequest::Pairing(client))
            .await
            .expect("provider server task is running");

        // Because there is currently an active Pairing Delegate, the `Provider` server should
        // reject the request - the upstream client `delegate_server` should terminate.
        let result = delegate_server.next().await;
        assert_matches!(result, None);
    }

    #[fuchsia::test]
    fn pairing_fidl_request_is_accepted() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let setup_fut = setup_provider();
        pin_mut!(setup_fut);
        let (provider, _le_peripheral, _gatt, _host_watcher, mut mock_pairing, _mock_upstream) =
            exec.run_singlethreaded(&mut setup_fut);
        let (mut sender, receiver) = mpsc::channel(0);
        let server_fut = provider.run(receiver);
        pin_mut!(server_fut);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // Shutting down and dropping the existing upstream delegate indicates client termination.
        let (delegate, new_upstream_delegate_server) =
            create_proxy_and_stream::<PairingDelegateMarker>().unwrap();
        let upstream_delegate_server = std::mem::replace(
            &mut mock_pairing.upstream_delegate_server,
            new_upstream_delegate_server,
        );
        upstream_delegate_server.control_handle().shutdown();
        drop(upstream_delegate_server);
        // Provider server should handle this without failing.
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // Simulate the FIDL client trying to set the Pairing Delegate. This is OK now that there no
        // longer is an active Pairing Delegate.
        let client =
            PairingArgs { input: InputCapability::None, output: OutputCapability::None, delegate };
        let send_fut = sender.send(ServiceRequest::Pairing(client));
        pin_mut!(send_fut);
        let (send_result, server_fut) = run_while(&mut exec, server_fut, send_fut);
        assert_matches!(send_result, Ok(_));

        // We expect the downstream pairing handler to receive the SetPairingDelegate request that
        // is made as a result of the aforementioned FIDL client request.
        let expect_fut = mock_pairing.expect_set_pairing_delegate();
        pin_mut!(expect_fut);
        let ((), _server_fut) = run_while(&mut exec, server_fut, expect_fut);
    }
}
