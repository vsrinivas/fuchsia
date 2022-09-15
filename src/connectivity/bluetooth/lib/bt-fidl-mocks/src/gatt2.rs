// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::expect::{expect_call, Status};
use anyhow::Error;
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_bluetooth::Uuid as FidlUuid;
use fidl_fuchsia_bluetooth_gatt2::{
    self as gatt2, Characteristic, CharacteristicNotifierMarker, ClientControlHandle, ClientMarker,
    ClientProxy, ClientRequest, ClientRequestStream, Handle, RemoteServiceMarker,
    RemoteServiceProxy, RemoteServiceReadByTypeResult, RemoteServiceRequest,
    RemoteServiceRequestStream, ServiceHandle,
};
use fuchsia_bluetooth::types::Uuid;
use fuchsia_zircon::Duration;

/// Provides a simple mock implementation of `fuchsia.bluetooth.gatt2.RemoteService`.
pub struct RemoteServiceMock {
    stream: RemoteServiceRequestStream,
    timeout: Duration,
}

impl RemoteServiceMock {
    pub fn new(timeout: Duration) -> Result<(RemoteServiceProxy, RemoteServiceMock), Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<RemoteServiceMarker>()?;
        Ok((proxy, RemoteServiceMock { stream, timeout }))
    }

    pub fn from_stream(stream: RemoteServiceRequestStream, timeout: Duration) -> RemoteServiceMock {
        RemoteServiceMock { stream, timeout }
    }

    pub async fn expect_discover_characteristics(
        &mut self,
        characteristics: &Vec<Characteristic>,
    ) -> Result<(), Error> {
        expect_call(&mut self.stream, self.timeout, move |req| match req {
            RemoteServiceRequest::DiscoverCharacteristics { responder } => {
                match responder.send(&mut characteristics.clone().into_iter()) {
                    Ok(_) => Ok(Status::Satisfied(())),
                    Err(e) => Err(e.into()),
                }
            }
            _ => Ok(Status::Pending),
        })
        .await
    }

    /// Wait until a Read By Type message is received with the given `uuid`. `result` will be sent
    /// in response to the matching FIDL request.
    pub async fn expect_read_by_type(
        &mut self,
        expected_uuid: Uuid,
        mut result: RemoteServiceReadByTypeResult,
    ) -> Result<(), Error> {
        let expected_uuid: FidlUuid = expected_uuid.into();
        expect_call(&mut self.stream, self.timeout, move |req| {
            if let RemoteServiceRequest::ReadByType { uuid, responder } = req {
                if uuid == expected_uuid {
                    responder.send(&mut result)?;
                    Ok(Status::Satisfied(()))
                } else {
                    // Send error to unexpected request.
                    responder.send(&mut Err(fidl_fuchsia_bluetooth_gatt2::Error::UnlikelyError))?;
                    Ok(Status::Pending)
                }
            } else {
                Ok(Status::Pending)
            }
        })
        .await
    }

    pub async fn expect_register_characteristic_notifier(
        &mut self,
        handle: Handle,
    ) -> Result<ClientEnd<CharacteristicNotifierMarker>, Error> {
        expect_call(&mut self.stream, self.timeout, move |req| match req {
            RemoteServiceRequest::RegisterCharacteristicNotifier {
                handle: h,
                notifier,
                responder,
            } => {
                if h == handle {
                    responder.send(&mut Ok(()))?;
                    Ok(Status::Satisfied(notifier))
                } else {
                    responder.send(&mut Err(gatt2::Error::InvalidHandle))?;
                    Ok(Status::Pending)
                }
            }
            _ => Ok(Status::Pending),
        })
        .await
    }
}

/// Mock for the fuchsia.bluetooth.gatt2/Client server. Can be used to expect and intercept requests
/// to connect to GATT services.
pub struct ClientMock {
    stream: ClientRequestStream,
    timeout: Duration,
}

impl ClientMock {
    pub fn new(timeout: Duration) -> Result<(ClientProxy, ClientMock), Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<ClientMarker>()?;
        Ok((proxy, ClientMock { stream, timeout }))
    }

    pub async fn expect_connect_to_service(
        &mut self,
        handle: ServiceHandle,
    ) -> Result<(ClientControlHandle, ServerEnd<RemoteServiceMarker>), Error> {
        expect_call(&mut self.stream, self.timeout, move |req| match req {
            ClientRequest::ConnectToService { handle: h, service, control_handle }
                if h == handle =>
            {
                Ok(Status::Satisfied((control_handle, service)))
            }
            _ => Ok(Status::Pending),
        })
        .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {crate::timeout_duration, futures::join};

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_expect_read_by_type() {
        let (proxy, mut mock) =
            RemoteServiceMock::new(timeout_duration()).expect("failed to create mock");
        let uuid = Uuid::new16(0x180d);
        let result: RemoteServiceReadByTypeResult = Ok(vec![]);

        let mut fidl_uuid: FidlUuid = uuid.clone().into();
        let read_by_type_fut = proxy.read_by_type(&mut fidl_uuid);
        let expect_fut = mock.expect_read_by_type(uuid, result);

        let (read_by_type_result, expect_result) = join!(read_by_type_fut, expect_fut);
        let _ = read_by_type_result.expect("read by type request failed");
        let _ = expect_result.expect("expectation not satisfied");
    }
}
