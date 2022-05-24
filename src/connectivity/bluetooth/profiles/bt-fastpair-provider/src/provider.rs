// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_bluetooth_sys::HostWatcherMarker;
use futures::{select, stream::StreamExt};
use tracing::{debug, warn};

use crate::advertisement::LowEnergyAdvertiser;
use crate::config::Config;
use crate::error::Error;
use crate::gatt_service::{GattRequest, GattService};
use crate::host_watcher::{HostEvent, HostWatcher};
use crate::types::AccountKeyList;

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
}

impl Provider {
    pub async fn new(config: Config) -> Result<Self, Error> {
        let advertiser = LowEnergyAdvertiser::new()?;
        let gatt = GattService::new(config.clone()).await?;
        let watcher = fuchsia_component::client::connect_to_protocol::<HostWatcherMarker>()?;
        let host_watcher = HostWatcher::new(watcher);
        Ok(Self { config, account_keys: AccountKeyList::new(), advertiser, gatt, host_watcher })
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

    async fn handle_gatt_update(&self, _update: GattRequest) {
        todo!("Handle GATT requests");
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
                        Some(update) => self.handle_gatt_update(update).await,
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
    use fidl_fuchsia_bluetooth_gatt2::LocalServiceProxy;
    use fidl_fuchsia_bluetooth_le::{PeripheralMarker, PeripheralRequestStream};
    use fidl_fuchsia_bluetooth_sys::HostWatcherRequestStream;
    use futures::pin_mut;
    use std::convert::TryFrom;

    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::HostId;

    use crate::gatt_service::tests::setup_gatt_service;
    use crate::host_watcher::tests::example_host;
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
}
