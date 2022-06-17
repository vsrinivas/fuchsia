// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_helpers::maybe_stream::MaybeStream;
use fidl_fuchsia_bluetooth_gatt2 as gatt;
use fidl_fuchsia_bluetooth_sys::HostWatcherMarker;
use fuchsia_bluetooth::types::PeerId;
use futures::{select, stream::StreamExt};
use tracing::{debug, info, warn};

use crate::advertisement::LowEnergyAdvertiser;
use crate::config::Config;
use crate::gatt_service::{GattRequest, GattService, GattServiceResponder};
use crate::host_watcher::{HostEvent, HostWatcher};
use crate::pairing::PairingManager;
use crate::types::keys::aes_from_anti_spoofing_and_public;
use crate::types::packets::{
    decrypt_key_based_pairing_request, decrypt_passkey_request, key_based_pairing_response,
    parse_key_based_pairing_request, passkey_response, KeyBasedPairingAction,
    KeyBasedPairingRequest,
};
use crate::types::{AccountKey, AccountKeyList, Error};

/// The toplevel server that manages the current state of the Fast Pair Provider service.
/// Owns the interfaces for interacting with various BT system services.
pub struct Provider {
    /// The configuration of the Fast Pair Provider component.
    config: Config,
    /// The current set of saved Account Keys.
    account_keys: AccountKeyList,
    /// Manages the Fast Pair advertisement over LE.
    advertiser: LowEnergyAdvertiser,
    /// The published Fast Pair GATT service. The remote peer (Seeker) will interact with this
    /// service to initiate Fast Pair flows.
    gatt: GattService,
    /// Watches for changes in the locally active Bluetooth Host.
    host_watcher: HostWatcher,
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
        Ok(Self {
            config,
            account_keys: AccountKeyList::new(),
            advertiser,
            gatt,
            host_watcher,
            pairing: MaybeStream::default(),
        })
    }

    async fn handle_host_watcher_update(&mut self, update: HostEvent) {
        match update {
            HostEvent::Discoverable(discoverable) | HostEvent::NewActiveHost { discoverable } => {
                let result = if discoverable {
                    self.advertiser.advertise_model_id(self.config.model_id).await
                } else {
                    self.advertiser.advertise_account_keys(&self.account_keys).await
                };
                let _ = result.map_err(|e| warn!("Couldn't advertise: {:?}", e));
            }
            HostEvent::NotAvailable => {
                // TODO(fxbug.dev/94166): It might make sense to shut down the GATT service.
                let _ = self.advertiser.stop_advertising().await;
            }
        }
    }

    /// Processes the encrypted key-based pairing `request`.
    /// Implements Steps 1 & 2 in the Pairing Procedure as defined in the GFPS specification. See
    /// https://developers.google.com/nearby/fast-pair/specifications/service/gatt#procedure
    ///
    /// Returns the decrypted request and associated key if the request was successfully decrypted.
    /// Returns Error otherwise.
    fn find_key_for_encrypted_request(
        &self,
        request: Vec<u8>,
    ) -> Result<(AccountKey, KeyBasedPairingRequest), Error> {
        // There must be a local Host to facilitate pairing.
        let local_address = self.host_watcher.address().ok_or(Error::internal("No active host"))?;

        let (encrypted_request, remote_public_key) = parse_key_based_pairing_request(request)?;
        let keys_to_try = if let Some(key) = remote_public_key {
            debug!("Trying remote public key");
            if !self.host_watcher.pairing_mode() {
                return Err(Error::internal("Active host is not discoverable"));
            }

            let aes_key = aes_from_anti_spoofing_and_public(&self.config.local_private_key, &key)?;
            vec![aes_key]
        } else {
            debug!("Trying saved Account Keys");
            self.account_keys.keys.iter().cloned().collect()
        };

        for key in keys_to_try {
            match decrypt_key_based_pairing_request(&encrypted_request, &key, &local_address) {
                Ok(request) => {
                    debug!("Found a valid key for pairing");
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
                info!("Couldn't decrypt request: {:?}", e);
                response(Err(gatt::Error::WriteRequestRejected));
                return;
            }
        };

        let pairing = if let Some(p) = self.pairing.inner_mut() {
            p
        } else {
            warn!("No Pairing Manager available to start key-based pairing");
            response(Err(gatt::Error::UnlikelyError));
            return;
        };

        // Notify the GATT service that the write request was successfully processed.
        response(Ok(()));

        let encrypted_response = key_based_pairing_response(
            &key,
            self.host_watcher.address().expect("active host"), // Guaranteed to exist.
        );
        if let Err(e) = self.gatt.notify_key_based_pairing(peer_id, encrypted_response) {
            warn!("Error notifying GATT characteristic: {:?}", e);
            return;
        }

        match pairing.new_pairing_procedure(peer_id, key) {
            Ok(_) => info!("Successfully started key-based pairing"),
            Err(e) => warn!("Couldn't start key-based pairing: {:?}", e),
        }

        // Some key-based pairing requests require additional steps.
        // TODO(fxbug.dev/96217): Track the salt in `request` to prevent replay attacks.
        match request.action {
            KeyBasedPairingAction::SeekerInitiatesPairing => {} // Seeker initiates subsequent flow
            KeyBasedPairingAction::ProviderInitiatesPairing { .. } => {
                // TODO(fxbug.dev96222): Use `sys.Access/Pair` to pair to peer.
            }
            KeyBasedPairingAction::RetroactiveWrite { .. } => {
                // TODO(fxbug.dev/99731): Support retroactive account key writes.
            }
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
            }
        }
    }

    fn handle_gatt_update(&mut self, update: GattRequest) {
        match update {
            GattRequest::KeyBasedPairing { peer_id, encrypted_request, response } => {
                self.handle_key_based_pairing_request(peer_id, encrypted_request, response);
            }
            GattRequest::VerifyPasskey { peer_id, encrypted_passkey, response } => {
                self.handle_verify_passkey_request(peer_id, encrypted_passkey, response);
            }
            _ => todo!("Implement GATT support"),
        }
    }

    /// Run the Fast Pair Provider service to completion.
    /// Terminates if an irrecoverable error is encountered, or all Fast Pair related resources
    /// have been exhausted.
    pub async fn run(mut self) -> Result<(), Error> {
        loop {
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
                        Some(update) => self.handle_host_watcher_update(update).await,
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
                            // TODO(fxbug.dev/98420): Notify FIDL client of success.
                            info!("Fast Pair pairing completed with peer: {:?}", id);
                        }
                    }
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
    use fidl_fuchsia_bluetooth_sys::HostWatcherRequestStream;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::HostId;
    use futures::{pin_mut, FutureExt};
    use std::convert::{TryFrom, TryInto};

    use crate::gatt_service::{
        tests::setup_gatt_service, KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
        PASSKEY_CHARACTERISTIC_HANDLE,
    };
    use crate::host_watcher::tests::example_host;
    use crate::pairing::tests::MockPairing;
    use crate::types::packets::tests::{KEY_BASED_PAIRING_REQUEST, PASSKEY_REQUEST};
    use crate::types::{keys, ModelId};

    async fn setup_provider(
    ) -> (Provider, PeripheralRequestStream, LocalServiceProxy, HostWatcherRequestStream, MockPairing)
    {
        let config = Config::example_config();

        let (peripheral_proxy, peripheral_server) =
            create_proxy_and_stream::<PeripheralMarker>().unwrap();
        let advertiser = LowEnergyAdvertiser::from_proxy(peripheral_proxy);

        let (gatt, local_service_proxy) = setup_gatt_service().await;

        let (watcher_client, watcher_server) =
            create_proxy_and_stream::<HostWatcherMarker>().unwrap();
        let host_watcher = HostWatcher::new(watcher_client);

        let (pairing, mock_pairing) = MockPairing::new_with_manager().await;

        let this = Provider {
            config,
            account_keys: AccountKeyList::new(),
            advertiser,
            gatt,
            host_watcher,
            pairing: Some(pairing).into(),
        };

        (this, peripheral_server, local_service_proxy, watcher_server, mock_pairing)
    }

    #[fuchsia::test]
    async fn terminates_if_host_watcher_closes() {
        let (provider, _peripheral, _gatt, host_watcher_stream, _mock_pairing) =
            setup_provider().await;
        let provider_fut = provider.run();
        pin_mut!(provider_fut);

        // Upstream `bt-gap` can no longer service HostWatcher requests.
        host_watcher_stream.control_handle().shutdown();
        drop(host_watcher_stream);

        let result = provider_fut.await;
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[fuchsia::test]
    async fn terminates_if_gatt_closes() {
        let (provider, _peripheral, gatt, _host_watcher, _mock_pairing) = setup_provider().await;
        let provider_fut = provider.run();
        pin_mut!(provider_fut);

        // Upstream bt-host no longer can support advertising the GATT service.
        drop(gatt);

        let result = provider_fut.await;
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[fuchsia::test]
    async fn advertisement_changes_upon_host_discoverable_change() {
        let (provider, mut le_peripheral, _gatt, mut host_watcher, _mock_pairing) =
            setup_provider().await;
        let provider_fut = provider.run();
        // The Provider server should never terminate. Always running.
        let _provider_server = fasync::Task::local(async move {
            let result = provider_fut.await;
            panic!("Provider server unexpectedly finished with result: {:?}", result);
        });

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

    fn server_task(provider: Provider) -> fasync::Task<()> {
        let provider_fut = provider.run();
        fasync::Task::local(async move {
            let result = provider_fut.await;
            panic!("Provider server unexpectedly finished with result: {:?}", result);
        })
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
        let (mut provider, _le_peripheral, gatt, _host_watcher, mut mock_pairing) =
            exec.run_singlethreaded(&mut setup_fut);

        // To avoid a bunch of unnecessary boilerplate involving the `host_watcher`, set the active
        // host with a known address.
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ true)
                .try_into()
                .unwrap(),
        );
        let server_fut = provider.run();
        pin_mut!(server_fut);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // Initiating a Key-based pairing request should succeed. The buffer is encrypted by the key
        // defined in the GFPS.
        let mut encrypted_buf = keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        encrypted_buf.append(&mut keys::tests::bob_public_key_bytes());
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypted_buf,
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
        let encrypted_buf = keys::tests::encrypt_message(&PASSKEY_REQUEST);
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            PASSKEY_CHARACTERISTIC_HANDLE,
            encrypted_buf,
            Ok(()),
            /* expect_item= */ true,
        );
        let (_, server_fut) = run_while(&mut exec, server_fut, write_fut);

        // The Fast Pair pairing procedure is not complete but the PairingDelegate request has been
        // responded to.
        let (pairing_result, _server_fut) = run_while(&mut exec, server_fut, pairing_fut);
        assert_eq!(pairing_result.unwrap(), (true, 0x123456));
    }

    #[fuchsia::test]
    async fn public_key_pairing_request_with_no_host() {
        let (provider, _le_peripheral, gatt, _host_watcher, _mock_pairing) = setup_provider().await;
        let _provider_server = server_task(provider);

        // Initiating a valid key-based pairing request should succeed and be handled gracefully.
        // Because there's no active host, we don't expect any subsequent GATT notification.
        let mut encrypted_buf = keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        encrypted_buf.append(&mut keys::tests::bob_public_key_bytes());
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypted_buf,
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    async fn public_key_pairing_request_with_not_discoverable_host() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing) =
            setup_provider().await;
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        let _provider_server = server_task(provider);

        // Initiating a key-based pairing request with public key should succeed and be handled
        // gracefully. Because the local host is not discoverable, we don't expect a subsequent GATT
        // notification.
        let mut encrypted_buf = keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        encrypted_buf.append(&mut keys::tests::bob_public_key_bytes());
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypted_buf,
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    async fn pairing_request_no_saved_keys() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing) =
            setup_provider().await;
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        let _provider_server = server_task(provider);

        // Initiating a key-based pairing request should be handled gracefully. Because there are no
        // saved Account Keys, pairing should not continue.
        let encrypted_buf = keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypted_buf,
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    async fn pairing_request_with_saved_key_notifies_gatt() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing) =
            setup_provider().await;
        // A active host that is not discoverable - expect to go through the subsequent pairing flow
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        // Two keys - one key should work, other is random.
        provider.account_keys.keys.push(AccountKey::new([2; 16]));
        provider.account_keys.keys.push(keys::tests::example_aes_key());
        let _provider_server = server_task(provider);

        // Initiating a key-based pairing request should be handled gracefully. Because there are no
        // saved Account Keys, pairing should not continue.
        let encrypted_buf = keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypted_buf,
            Ok(()),
            /* expect_item= */ true,
        )
        .await;
    }

    #[fuchsia::test]
    async fn passkey_write_with_no_active_pairing_procedure() {
        let (mut provider, _le_peripheral, gatt, _host_watcher, _mock_pairing) =
            setup_provider().await;
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        let _provider_server = server_task(provider);

        // Initiating a passkey write request should be handled gracefully. Because there is no
        // active pairing procedure (e.g a key-based pairing write was never received), pairing
        // should not continue and the GATT request should result in Error.
        let encrypted_buf = keys::tests::encrypt_message(&PASSKEY_REQUEST);
        gatt_write_results_in_expected_notification(
            &gatt,
            PASSKEY_CHARACTERISTIC_HANDLE,
            encrypted_buf,
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
        let (mut provider, _le_peripheral, gatt, _host_watcher, mut mock_pairing) =
            exec.run_singlethreaded(&mut setup_fut);

        // Set the active host with a known address.
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ true)
                .try_into()
                .unwrap(),
        );
        let server_fut = provider.run();
        pin_mut!(server_fut);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("still active");

        // Key-based pairing begins.
        let mut encrypted_buf = keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        encrypted_buf.append(&mut keys::tests::bob_public_key_bytes());
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE,
            encrypted_buf,
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
        let encrypted_buf = keys::tests::encrypt_message(&PASSKEY_REQUEST);
        let write_fut = gatt_write_results_in_expected_notification(
            &gatt,
            PASSKEY_CHARACTERISTIC_HANDLE,
            encrypted_buf,
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        );
        let (_, _server_fut) = run_while(&mut exec, server_fut, write_fut);
    }
}
