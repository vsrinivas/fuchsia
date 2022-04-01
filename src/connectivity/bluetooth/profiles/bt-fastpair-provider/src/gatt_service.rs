// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use core::{
    pin::Pin,
    task::{Context, Poll},
};
use fidl::endpoints::{create_request_stream, ControlHandle, RequestStream};
use fidl_fuchsia_bluetooth_gatt2::{
    self as gatt, AttributePermissions, Characteristic, CharacteristicPropertyBits, Handle,
    LocalServiceMarker, LocalServiceRequest, LocalServiceRequestStream, SecurityRequirements,
    Server_Marker, Server_Proxy, ServiceInfo,
};
use fuchsia_bluetooth::types::Uuid;
use fuchsia_component::client::connect_to_protocol;
use futures::ready;
use futures::stream::{FusedStream, Stream, StreamExt};
use std::str::FromStr;
use tracing::{trace, warn};

/// The UUID of the Fast Pair Service.
const FAST_PAIR_SERVICE_UUID: u16 = 0xFE2C;

/// Custom characteristic - Model ID.
const MODEL_ID_CHARACTERISTIC_UUID: &str = "FE2C1233-8366-4814-8EB0-01DE32100BEA";
/// Fixed Handle assigned to the Model ID characteristic.
const MODEL_ID_CHARACTERISTIC_HANDLE: Handle = Handle { value: 1 };

/// Custom characteristic - Key-based pairing.
const KEY_BASED_PAIRING_CHARACTERISTIC_UUID: &str = "FE2C1234-8366-4814-8EB0-01DE32100BEA";
/// Fixed Handle assigned to the Model ID characteristic.
const KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE: Handle = Handle { value: 2 };

/// Custom characteristic - Passkey.
const PASSKEY_CHARACTERISTIC_UUID: &str = "FE2C1235-8366-4814-8EB0-01DE32100BEA";
/// Fixed Handle assigned to the Model ID characteristic.
const PASSKEY_CHARACTERISTIC_HANDLE: Handle = Handle { value: 3 };

/// Custom characteristic - Account Key.
const ACCOUNT_KEY_CHARACTERISTIC_UUID: &str = "FE2C1236-8366-4814-8EB0-01DE32100BEA";
/// Fixed Handle assigned to the Model ID characteristic.
const ACCOUNT_KEY_CHARACTERISTIC_HANDLE: Handle = Handle { value: 4 };

/// Standard characteristic - the firmware revision of the device.
const FIRMWARE_REVISION_CHARACTERISTIC_UUID: u16 = 0x2A26;
/// Fixed Handle assigned to the Model ID characteristic.
const FIRMWARE_REVISION_CHARACTERISTIC_HANDLE: Handle = Handle { value: 5 };

// TODO(fxbug.dev/95542): Define variants for each type of GATT request.
#[derive(Debug)]
pub struct GattRequest {}

/// Represents a published Fast Pair Provider GATT service.
pub struct GattService {
    /// The connection to the `fuchsia.bluetooth.gatt2.Server` capability.
    server_svc: Server_Proxy,
    /// The stream associated with the published Fast Pair GATT Service. Receives GATT requests
    /// initiated by the remote peer.
    local_service_server: LocalServiceRequestStream,
}

impl std::fmt::Debug for GattService {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("GattService").field("server_svc", &self.server_svc).finish()
    }
}

impl GattService {
    /// Builds and returns a published Fast Pair Provider GATT service.
    pub async fn new() -> Result<Self, Error> {
        let gatt_server_proxy =
            connect_to_protocol::<Server_Marker>().context("Can't connect to gatt2.Server")?;
        Self::from_proxy(gatt_server_proxy).await
    }

    async fn from_proxy(proxy: Server_Proxy) -> Result<Self, Error> {
        let local_service = Self::publish_service(&proxy).await?;
        Ok(Self { server_svc: proxy, local_service_server: local_service })
    }

    // Builds and returns the characteristics associated with the Fast Pair Provider GATT service.
    fn characteristics() -> Vec<Characteristic> {
        // There are 5 characteristics that are published in this service.

        // 0. Model ID - This only supports read with no specific security requirements.
        let model_id = Characteristic {
            handle: Some(MODEL_ID_CHARACTERISTIC_HANDLE),
            type_: Uuid::from_str(MODEL_ID_CHARACTERISTIC_UUID).ok().map(Into::into),
            properties: Some(CharacteristicPropertyBits::READ.bits().into()),
            permissions: Some(AttributePermissions {
                read: Some(SecurityRequirements::EMPTY),
                ..AttributePermissions::EMPTY
            }),
            ..Characteristic::EMPTY
        };

        // 1. Key-based Pairing - This supports write & notify with no specific security
        //    requirements.
        let key_based_pairing = Characteristic {
            handle: Some(KEY_BASED_PAIRING_CHARACTERISTIC_HANDLE),
            type_: Uuid::from_str(KEY_BASED_PAIRING_CHARACTERISTIC_UUID).ok().map(Into::into),
            properties: Some(
                (CharacteristicPropertyBits::WRITE | CharacteristicPropertyBits::NOTIFY)
                    .bits()
                    .into(),
            ),
            permissions: Some(AttributePermissions {
                write: Some(SecurityRequirements::EMPTY),
                update: Some(SecurityRequirements::EMPTY),
                ..AttributePermissions::EMPTY
            }),
            ..Characteristic::EMPTY
        };

        // 2. Passkey - This supports write & notify with no specific security requirements.
        let passkey = Characteristic {
            handle: Some(PASSKEY_CHARACTERISTIC_HANDLE),
            type_: Uuid::from_str(PASSKEY_CHARACTERISTIC_UUID).ok().map(Into::into),
            properties: Some(
                (CharacteristicPropertyBits::WRITE | CharacteristicPropertyBits::NOTIFY)
                    .bits()
                    .into(),
            ),
            permissions: Some(AttributePermissions {
                write: Some(SecurityRequirements::EMPTY),
                update: Some(SecurityRequirements::EMPTY),
                ..AttributePermissions::EMPTY
            }),
            ..Characteristic::EMPTY
        };

        // 3. Account Key - This only supports write with no specific security requirements.
        let account_key = Characteristic {
            handle: Some(ACCOUNT_KEY_CHARACTERISTIC_HANDLE),
            type_: Uuid::from_str(ACCOUNT_KEY_CHARACTERISTIC_UUID).ok().map(Into::into),
            properties: Some(CharacteristicPropertyBits::WRITE.bits().into()),
            permissions: Some(AttributePermissions {
                write: Some(SecurityRequirements::EMPTY),
                ..AttributePermissions::EMPTY
            }),
            ..Characteristic::EMPTY
        };

        // 4. Firmware Revision - This only supports read with no specific security requirements.
        let firmware_revision = Characteristic {
            handle: Some(FIRMWARE_REVISION_CHARACTERISTIC_HANDLE),
            type_: Some(Uuid::new16(FIRMWARE_REVISION_CHARACTERISTIC_UUID).into()),
            properties: Some(CharacteristicPropertyBits::READ.bits().into()),
            permissions: Some(AttributePermissions {
                read: Some(SecurityRequirements::EMPTY),
                ..AttributePermissions::EMPTY
            }),
            ..Characteristic::EMPTY
        };

        vec![model_id, key_based_pairing, passkey, account_key, firmware_revision]
    }

    // Returns the GATT service definition for the Fast Pair Provider role.
    fn service_info() -> ServiceInfo {
        ServiceInfo {
            primary: Some(true),
            type_: Some(Uuid::new16(FAST_PAIR_SERVICE_UUID).into()),
            characteristics: Some(Self::characteristics()),
            ..ServiceInfo::EMPTY
        }
    }

    /// Publishes a Fast Pair Provider GATT service using the provided `server_svc`.
    ///
    /// Returns the server end of the published service.
    async fn publish_service(
        server_svc: &Server_Proxy,
    ) -> Result<LocalServiceRequestStream, Error> {
        // Build the GATT service.
        let info = Self::service_info();
        let (service_client, service_stream) = create_request_stream::<LocalServiceMarker>()
            .context("Can't create LocalService endpoints")?;

        if let Err(e) = server_svc.publish_service(info, service_client).await? {
            warn!("Couldn't set up Fast Pair GATT Service: {:?}", e);
            // TODO(fxbug.dev/94166): Define component-local Error type and use it here.
            return Err(format_err!("{:?}", e));
        }

        Ok(service_stream)
    }

    /// Handle an incoming FIDL request for the local GATT service.
    fn handle_service_request(&mut self, request: LocalServiceRequest) -> Option<GattRequest> {
        // TODO(fxbug.dev/95542): Handle each GATT request type.
        match request {
            LocalServiceRequest::ReadValue { responder, .. } => {
                let _ = responder.send(&mut Err(gatt::Error::UnlikelyError));
            }
            LocalServiceRequest::WriteValue { responder, .. } => {
                let _ = responder.send(&mut Err(gatt::Error::UnlikelyError));
            }
            LocalServiceRequest::CharacteristicConfiguration { responder, .. } => {
                let _ = responder.send();
            }
            LocalServiceRequest::ValueChangedCredit { .. } => {}
        }
        None
    }
}

impl Stream for GattService {
    type Item = GattRequest;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        // Keep polling the request stream until it produces a request that should be returned or it
        // produces Poll::Pending.
        loop {
            let result = ready!(self.local_service_server.poll_next_unpin(cx));

            let result = match result {
                Some(Ok(request)) => match self.handle_service_request(request) {
                    None => continue,
                    request => request,
                },
                Some(Err(e)) => {
                    warn!("Error in LocalService FIDL client request: {}.", e);
                    None
                }
                None => None,
            };
            // Either the `LocalService` stream is exhausted, or there is an error in the channel.
            if result.is_none() {
                trace!("Closing LocalService connection");
                self.local_service_server.control_handle().shutdown();
            }

            return Poll::Ready(result);
        }
    }
}

impl FusedStream for GattService {
    fn is_terminated(&self) -> bool {
        self.local_service_server.is_terminated()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use async_utils::PollExt;
    use fidl_fuchsia_bluetooth_gatt2::LocalServiceProxy;
    use fuchsia_async as fasync;
    use futures::{future::Either, pin_mut, stream::StreamExt};

    #[fuchsia::test]
    fn gatt_service_is_received_by_upstream_server() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (gatt_client, mut gatt_server) =
            fidl::endpoints::create_proxy_and_stream::<Server_Marker>().unwrap();
        let gatt_server_fut = gatt_server.next();
        pin_mut!(gatt_server_fut);
        let _ = exec.run_until_stalled(&mut gatt_server_fut).expect_pending("Upstream still ok");

        let publish_fut = GattService::from_proxy(gatt_client);
        pin_mut!(publish_fut);
        let _ =
            exec.run_until_stalled(&mut publish_fut).expect_pending("Waiting for publish response");

        // Expect the upstream server to receive the publish request.
        let (_info, _service_client, responder) =
            match exec.run_until_stalled(&mut gatt_server_fut).expect("stream is ready") {
                Some(Ok(req)) => req.into_publish_service().expect("only possible request"),
                x => panic!("Expected ready request but got: {:?}", x),
            };
        // Upstream server responds positively.
        let _ = responder.send(&mut Ok({}));

        // Publish service request should resolve successfully.
        let publish_result =
            exec.run_until_stalled(&mut publish_fut).expect("publish response received");
        assert_matches!(publish_result, Ok(_));
    }

    /// Builds the `GattService` by publishing the Fast Pair Provider service definition.
    /// Returns the `GattService` and the connection to the published local service.
    async fn setup_gatt_service() -> (GattService, LocalServiceProxy) {
        let (gatt_server_client, mut gatt_server) =
            fidl::endpoints::create_proxy_and_stream::<Server_Marker>().unwrap();

        let publish_fut = GattService::from_proxy(gatt_server_client);
        let gatt_server_fut = gatt_server.select_next_some();
        pin_mut!(publish_fut);
        pin_mut!(gatt_server_fut);

        match futures::future::select(publish_fut, gatt_server_fut).await {
            Either::Left(_) => panic!("Publish future resolved before GATT server responded"),
            Either::Right((server_result, publish_fut)) => {
                let (_info, local_service_client, responder) = server_result
                    .expect("valid FIDL request")
                    .into_publish_service()
                    .expect("only possible request");
                // Respond positively.
                let _ = responder.send(&mut Ok({}));
                // Publish service request should resolve.
                let gatt_service = publish_fut.await.expect("should resolve ok");
                (gatt_service, local_service_client.into_proxy().unwrap())
            }
        }
    }

    #[fuchsia::test]
    fn gatt_service_stream_impl_terminates() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (gatt_service, _upstream_service_client) =
            exec.run_singlethreaded(setup_gatt_service());
        pin_mut!(gatt_service);

        assert!(!gatt_service.is_terminated());
        let _ =
            exec.run_until_stalled(&mut gatt_service.next()).expect_pending("stream still active");

        // Upstream terminates the connection for the local service.
        drop(_upstream_service_client);

        // The stream should be exhausted.
        let result =
            exec.run_until_stalled(&mut gatt_service.next()).expect("stream item should resolve");
        assert_matches!(result, None);
        assert!(gatt_service.is_terminated());
    }
}
