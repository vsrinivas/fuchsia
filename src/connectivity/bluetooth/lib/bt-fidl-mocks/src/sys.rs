// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::expect::{expect_call, Status},
    anyhow::Error,
    fidl_fuchsia_bluetooth::PeerId,
    fidl_fuchsia_bluetooth_sys::{
        AccessMarker, AccessProxy, AccessRequest, AccessRequestStream, Error as AccessError,
        PairingOptions,
    },
    fuchsia_zircon::Duration,
};

/// Provides a simple mock implementation of `fuchsia.bluetooth.sys.Access`.
pub struct AccessMock {
    stream: AccessRequestStream,
    timeout: Duration,
}

impl AccessMock {
    pub fn new(timeout: Duration) -> Result<(AccessProxy, AccessMock), Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<AccessMarker>()?;
        Ok((proxy, AccessMock { stream, timeout }))
    }

    pub async fn expect_disconnect(
        &mut self,
        expected_peer_id: PeerId,
        mut result: Result<(), AccessError>,
    ) -> Result<(), Error> {
        expect_call(&mut self.stream, self.timeout, move |req| match req {
            AccessRequest::Disconnect { id, responder } if id == expected_peer_id => {
                responder.send(&mut result)?;
                Ok(Status::Satisfied(()))
            }
            _ => Ok(Status::Pending),
        })
        .await
    }

    pub async fn expect_forget(
        &mut self,
        expected_peer_id: PeerId,
        mut result: Result<(), AccessError>,
    ) -> Result<(), Error> {
        expect_call(&mut self.stream, self.timeout, move |req| match req {
            AccessRequest::Forget { id, responder } if id == expected_peer_id => {
                responder.send(&mut result)?;
                Ok(Status::Satisfied(()))
            }
            _ => Ok(Status::Pending),
        })
        .await
    }

    pub async fn expect_pair(
        &mut self,
        expected_peer_id: PeerId,
        expected_options: PairingOptions,
        mut result: Result<(), AccessError>,
    ) -> Result<(), Error> {
        expect_call(&mut self.stream, self.timeout, move |req| match req {
            AccessRequest::Pair { id, options, responder }
                if id == expected_peer_id && options == expected_options =>
            {
                responder.send(&mut result)?;
                Ok(Status::Satisfied(()))
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
    async fn test_expect_disconnect() {
        let (proxy, mut mock) = AccessMock::new(timeout_duration()).expect("failed to create mock");
        let mut peer_id = PeerId { value: 1 };

        let disconnect = proxy.disconnect(&mut peer_id);
        let expect = mock.expect_disconnect(peer_id, Ok(()));

        let (disconnect_result, expect_result) = join!(disconnect, expect);
        let _ = disconnect_result.expect("disconnect request failed");
        let _ = expect_result.expect("expectation not satisfied");
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_expect_forget() {
        let (proxy, mut mock) = AccessMock::new(timeout_duration()).expect("failed to create mock");
        let mut peer_id = PeerId { value: 1 };

        let forget = proxy.forget(&mut peer_id);
        let expect = mock.expect_forget(peer_id, Ok(()));

        let (forget_result, expect_result) = join!(forget, expect);
        let _ = forget_result.expect("forget request failed");
        let _ = expect_result.expect("expectation not satisifed");
    }
}
