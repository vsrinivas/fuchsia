// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_gatt2 as gatt;
use fidl_fuchsia_bluetooth_sys::HostWatcherMarker;
use fuchsia_bluetooth::types::{Address, PeerId};
use futures::{select, stream::StreamExt};
use tracing::{debug, info, warn};

use crate::advertisement::LowEnergyAdvertiser;
use crate::config::Config;
use crate::error::Error;
use crate::gatt_service::{GattRequest, GattService, GattServiceResponder};
use crate::host_watcher::{HostEvent, HostWatcher};
use crate::keys::aes_from_anti_spoofing_and_public;
use crate::packets::{
    decrypt_key_based_pairing_request, parse_key_based_pairing_request, KeyBasedPairingAction,
    KeyBasedPairingRequest,
};
use crate::types::{AccountKey, AccountKeyList};

/// The toplevel server that manages the current state of the Fast Pair Provider service.
/// Owns the interfaces for interacting with various BT system services.
pub struct Provider {
    /// The configuration of the Fast Pair Provider component.
    config: Config,
    /// The current set of saved Account Keys.
    account_keys: AccountKeyList,
    /// The saved Account Key representing an active key-based pairing procedure between the local
    /// device and remote peer.
    // TODO(fxbug.dev/99757): This key should be cleared if pairing has not started after 10
    // seconds. We'll likely want to be able to support multiple concurrent pairing requests from
    // different peers so this may better be suited as a HashMap<PeerId, V>.
    active_pairing_key: Option<(PeerId, AccountKey)>,
    /// Manages the Fast Pair advertisement over LE.
    advertiser: LowEnergyAdvertiser,
    /// The published Fast Pair GATT service. The remote peer (Seeker) will interact with this
    /// service to initiate Fast Pair flows.
    gatt: GattService,
    /// Watches for changes in the locally active Bluetooth Host.
    host_watcher: HostWatcher,
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
            active_pairing_key: None,
            advertiser,
            gatt,
            host_watcher,
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

    /// Builds and returns an encrypted key-based pairing response.
    fn key_based_pairing_response(key: &AccountKey, local_address: Address) -> Vec<u8> {
        let mut response = [0; 16];
        // First byte indicates key-based pairing response.
        response[0] = 0x01;
        // Next 6 bytes is the local BR/EDR address.
        response[1..7].copy_from_slice(local_address.bytes());
        // Final 9 bytes is a randomly generated salt value.
        fuchsia_zircon::cprng_draw(&mut response[7..16]);
        key.encrypt(&response).to_vec()
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

        // Notify the GATT service that the write request was successful.
        response(Ok(()));
        info!("Successfully started key-based pairing");

        let encrypted_response = Self::key_based_pairing_response(
            &key,
            self.host_watcher.address().expect("active host"), // Guaranteed to exist.
        );
        match self.gatt.notify_key_based_pairing(peer_id, encrypted_response) {
            Ok(_) => {
                // TODO(fxbug.dev/99757): This key is only valid for requests from
                // `peer_id` and should be persisted for at most 10 seconds.
                self.active_pairing_key = Some((peer_id, key));
            }
            Err(e) => {
                warn!("Error notifying GATT characteristic: {:?}", e);
                return;
            }
        }

        // Some key-based pairing requests require additional steps.
        match request.action {
            KeyBasedPairingAction::SeekerInitiatesPairing => {} // Seeker initiates subsequent flow
            KeyBasedPairingAction::ProviderInitiatesPairing { .. } => {
                // TODO(fxbug.dev96222): Use `sys.Access/Pair` to pair to peer.
            }
            KeyBasedPairingAction::RetroactiveWrite { .. } => {
                // TODO(fxbug.dev/99731): Support retroactive account key writes.
            }
        }
        // TODO(fxbug.dev/96217): Track the salt in `request` to prevent replay attacks.
    }

    fn handle_gatt_update(&mut self, update: GattRequest) {
        match update {
            GattRequest::KeyBasedPairing { peer_id, encrypted_request, response } => {
                self.handle_key_based_pairing_request(peer_id, encrypted_request, response);
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
    use fidl::endpoints::{create_proxy_and_stream, ControlHandle, Proxy, RequestStream};
    use fidl_fuchsia_bluetooth_gatt2::{
        LocalServiceProxy, ValueChangedParameters, WriteValueParameters,
    };
    use fidl_fuchsia_bluetooth_le::{PeripheralMarker, PeripheralRequestStream};
    use fidl_fuchsia_bluetooth_sys::HostWatcherRequestStream;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::HostId;
    use futures::{pin_mut, FutureExt};
    use std::convert::{TryFrom, TryInto};

    use crate::gatt_service::{tests::setup_gatt_service, KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE};
    use crate::host_watcher::tests::example_host;
    use crate::packets::tests::KEY_BASED_PAIRING_REQUEST;
    use crate::types::ModelId;

    async fn setup_provider(
    ) -> (Provider, PeripheralRequestStream, LocalServiceProxy, HostWatcherRequestStream) {
        let config = Config::example_config();

        let (peripheral_proxy, peripheral_server) =
            create_proxy_and_stream::<PeripheralMarker>().unwrap();
        let advertiser = LowEnergyAdvertiser::from_proxy(peripheral_proxy);

        let (gatt, local_service_proxy) = setup_gatt_service().await;

        let (watcher_client, watcher_server) =
            create_proxy_and_stream::<HostWatcherMarker>().unwrap();
        let host_watcher = HostWatcher::new(watcher_client);

        let this = Provider {
            config,
            account_keys: AccountKeyList::new(),
            active_pairing_key: None,
            advertiser,
            gatt,
            host_watcher,
        };

        (this, peripheral_server, local_service_proxy, watcher_server)
    }

    #[fuchsia::test]
    async fn terminates_if_host_watcher_closes() {
        let (provider, _peripheral, _gatt, host_watcher_stream) = setup_provider().await;
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
        let (provider, _peripheral, gatt, _host_watcher) = setup_provider().await;
        let provider_fut = provider.run();
        pin_mut!(provider_fut);

        // Upstream bt-host no longer can support advertising the GATT service.
        drop(gatt);

        let result = provider_fut.await;
        assert_matches!(result, Err(Error::InternalError(_)));
    }

    #[fuchsia::test]
    async fn advertisement_changes_upon_host_discoverable_change() {
        let (provider, mut le_peripheral, _gatt, mut host_watcher) = setup_provider().await;
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

    async fn pairing_gatt_write_results_in_expected_notification(
        gatt: &LocalServiceProxy,
        encrypted_buf: Vec<u8>,
        expected_result: Result<(), gatt::Error>,
        expect_item: bool,
    ) {
        let result = gatt
            .write_value(WriteValueParameters {
                peer_id: Some(PeerId(123).into()),
                handle: Some(KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE),
                offset: Some(0),
                value: Some(encrypted_buf),
                ..WriteValueParameters::EMPTY
            })
            .await
            .expect("valid FIDL request");
        assert_eq!(result, expected_result);

        let mut stream = gatt.take_event_stream();
        if expect_item {
            let ValueChangedParameters { peer_ids, handle, .. } = stream
                .select_next_some()
                .await
                .expect("valid event")
                .into_on_notify_value()
                .expect("notification");
            assert_eq!(peer_ids, Some(vec![PeerId(123).into()]));
            assert_eq!(handle, Some(KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE));
        } else {
            assert_matches!(stream.next().now_or_never(), None);
        }
    }

    #[fuchsia::test]
    async fn public_key_pairing_request_key_notifies_gatt() {
        let (mut provider, _le_peripheral, gatt, _host_watcher) = setup_provider().await;
        // To avoid a bunch of unnecessary boilerplate involving the `host_watcher`, set the active
        // host with a known address.
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ true)
                .try_into()
                .unwrap(),
        );
        let _provider_server = server_task(provider);

        // Initiating a Key-based pairing request should succeed. The buffer is encrypted by the key
        // defined in the GFPS.
        let mut encrypted_buf = crate::keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        encrypted_buf.append(&mut crate::keys::tests::bob_public_key_bytes());
        pairing_gatt_write_results_in_expected_notification(
            &gatt,
            encrypted_buf,
            Ok(()),
            /* expect_item= */ true,
        )
        .await;
    }

    #[fuchsia::test]
    async fn public_key_pairing_request_with_no_host() {
        let (provider, _le_peripheral, gatt, _host_watcher) = setup_provider().await;
        let _provider_server = server_task(provider);

        // Initiating a valid key-based pairing request should be rejected.
        // Because there's no active host, we don't expect any subsequent GATT notification.
        let mut encrypted_buf = crate::keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        encrypted_buf.append(&mut crate::keys::tests::bob_public_key_bytes());
        pairing_gatt_write_results_in_expected_notification(
            &gatt,
            encrypted_buf,
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    async fn public_key_pairing_request_with_not_discoverable_host() {
        let (mut provider, _le_peripheral, gatt, _host_watcher) = setup_provider().await;
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        let _provider_server = server_task(provider);

        // Initiating a key-based pairing request with public key should be handled gracefully.
        // Because the local host is not discoverable, the write request should be rejected and no
        // subsequent GATT notification.
        let mut encrypted_buf = crate::keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        encrypted_buf.append(&mut crate::keys::tests::bob_public_key_bytes());
        pairing_gatt_write_results_in_expected_notification(
            &gatt,
            encrypted_buf,
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    async fn pairing_request_no_saved_keys() {
        let (mut provider, _le_peripheral, gatt, _host_watcher) = setup_provider().await;
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        let _provider_server = server_task(provider);

        // Initiating a key-based pairing request should be handled gracefully. Because there are no
        // saved Account Keys, pairing should not continue.
        let encrypted_buf = crate::keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        pairing_gatt_write_results_in_expected_notification(
            &gatt,
            encrypted_buf,
            Err(gatt::Error::WriteRequestRejected),
            /* expect_item= */ false,
        )
        .await;
    }

    #[fuchsia::test]
    async fn pairing_request_with_saved_key_notifies_gatt() {
        let (mut provider, _le_peripheral, gatt, _host_watcher) = setup_provider().await;
        // A active host that is not discoverable - expect to go through the subsequent pairing flow
        provider.host_watcher.set_active_host(
            example_host(HostId(1), /* active= */ true, /* discoverable= */ false)
                .try_into()
                .unwrap(),
        );
        // Two keys - one key should work, other is random.
        provider.account_keys.keys.push(AccountKey::new([2; 16]));
        provider.account_keys.keys.push(crate::keys::tests::example_aes_key());
        let _provider_server = server_task(provider);

        // Initiating a key-based pairing request should succeed because there is a saved Account
        // Key that can successfully decrypt the message.
        let encrypted_buf = crate::keys::tests::encrypt_message(&KEY_BASED_PAIRING_REQUEST);
        pairing_gatt_write_results_in_expected_notification(
            &gatt,
            encrypted_buf,
            Ok(()),
            /* expect_item= */ true,
        )
        .await;
    }
}
