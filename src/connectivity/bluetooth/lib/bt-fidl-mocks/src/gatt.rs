// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::expect::{expect_call, Status},
    anyhow::Error,
    fidl_fuchsia_bluetooth::Uuid as FidlUuid,
    fidl_fuchsia_bluetooth_gatt::{
        RemoteServiceMarker, RemoteServiceProxy, RemoteServiceReadByTypeResult,
        RemoteServiceRequest, RemoteServiceRequestStream,
    },
    fuchsia_bluetooth::types::Uuid,
    fuchsia_zircon::Duration,
};

/// Provides a simple mock implementation of `fuchsia.bluetooth.gatt.RemoteService`.
pub struct RemoteServiceMock {
    stream: RemoteServiceRequestStream,
    timeout: Duration,
}

impl RemoteServiceMock {
    pub fn new(timeout: Duration) -> Result<(RemoteServiceProxy, RemoteServiceMock), Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<RemoteServiceMarker>()?;
        Ok((proxy, RemoteServiceMock { stream, timeout }))
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
                    responder.send(&mut Err(fidl_fuchsia_bluetooth_gatt::Error::Failure))?;
                    Ok(Status::Pending)
                }
            } else {
                Ok(Status::Pending)
            }
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
        let read_by_type = proxy.read_by_type(&mut fidl_uuid);
        let expect = mock.expect_read_by_type(uuid, result);

        let (read_by_type_result, expect_result) = join!(read_by_type, expect);
        let _ = read_by_type_result.expect("read by type request failed");
        let _ = expect_result.expect("expectation not satisfied");
    }
}
