// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Error};
use fidl::endpoints::create_request_stream;
use fidl_fuchsia_bluetooth_gatt2::*;
use fuchsia_bluetooth::types::Uuid;
use fuchsia_component::client::connect_to_protocol;
use std::str::FromStr;
use tracing::warn;

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

pub struct GattService {
    server_svc: Server_Proxy,
}

impl GattService {
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

    // TODO(fxbug.dev/95542): Uncomment this when the GATT Service is used in this component.
    #[allow(unused)]
    pub async fn publish_service(&self) -> Result<LocalServiceRequestStream, Error> {
        // Build the GATT service.
        let info = Self::service_info();
        let (service_client, service_stream) = create_request_stream::<LocalServiceMarker>()
            .context("Can't create LocalService endpoints")?;

        if let Err(e) = self.server_svc.publish_service(info, service_client).await? {
            warn!("Couldn't set up Fast Pair GATT Service: {:?}", e);
            // TODO(fxbug.dev/94166): Define component-local Error type and use it here.
            return Err(format_err!("{:?}", e));
        }

        // TODO(fxbug.dev/95542): Save the `service_stream` within this object and implement Stream
        // for `GattService` to propagate LocalService events to the component.
        Ok(service_stream)
    }

    fn from_proxy(proxy: Server_Proxy) -> Self {
        Self { server_svc: proxy }
    }

    pub fn new() -> Result<Self, Error> {
        let gatt_server_proxy =
            connect_to_protocol::<Server_Marker>().context("Can't connect to gatt2.Server")?;
        Ok(Self::from_proxy(gatt_server_proxy))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use async_utils::PollExt;
    use fuchsia_async as fasync;
    use futures::{pin_mut, stream::StreamExt};

    #[test]
    fn gatt_service_is_received_by_upstream_server() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (gatt_client, mut gatt_server) =
            fidl::endpoints::create_proxy_and_stream::<Server_Marker>().unwrap();
        let gatt_server_fut = gatt_server.next();
        pin_mut!(gatt_server_fut);
        let _ = exec.run_until_stalled(&mut gatt_server_fut).expect_pending("Upstream still ok");

        let service = GattService::from_proxy(gatt_client);

        let publish_fut = service.publish_service();
        pin_mut!(publish_fut);
        let _ =
            exec.run_until_stalled(&mut publish_fut).expect_pending("Waiting for publish response");

        // Expect the upstream server to receive the publish request.
        let (_info, _service_client, responder) =
            match exec.run_until_stalled(&mut gatt_server_fut).expect("stream is ready") {
                Some(Ok(req)) => req.into_publish_service().expect("only possible request"),
                x => panic!("Expected ready request but got: {:?}", x),
            };
        // Respond positively.
        let _ = responder.send(&mut Ok({}));

        // Publish service request should resolve.
        let _gatt_service_stream = exec
            .run_until_stalled(&mut publish_fut)
            .expect("publish response received")
            .expect("result is Ok");
    }
}
