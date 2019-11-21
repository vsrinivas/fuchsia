// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::expect::{expect_call, Status},
    failure::Error,
    fidl_fuchsia_bluetooth::Status as FidlStatus,
    fidl_fuchsia_bluetooth_control::{
        ControlMarker, ControlProxy, ControlRequest, ControlRequestStream,
    },
    fuchsia_bluetooth::bt_fidl_status,
    fuchsia_zircon::Duration,
};

/// Provides a simple mock implementation of `fuchsia.bluetooth.control.Control`.
pub struct ControlMock {
    stream: ControlRequestStream,
    timeout: Duration,
}

impl ControlMock {
    pub fn new(timeout: Duration) -> Result<(ControlProxy, ControlMock), Error> {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<ControlMarker>()?;
        Ok((proxy, ControlMock { stream, timeout }))
    }

    /// Wait until a Disconnect message is received with the given `peer_id`. `status` will be sent
    /// in response to the matching FIDL request.
    pub async fn expect_disconnect(
        &mut self,
        peer_id: String,
        mut status: FidlStatus,
    ) -> Result<(), Error> {
        expect_call(&mut self.stream, self.timeout, move |req| {
            Ok(match req {
                ControlRequest::Disconnect { device_id, responder } => {
                    if device_id == peer_id {
                        responder.send(&mut status)?;
                        Status::Satisfied(())
                    } else {
                        // Respond with success for the ignored call.
                        responder.send(&mut bt_fidl_status!())?;
                        Status::Pending
                    }
                }
                _ => Status::Pending,
            })
        })
        .await
    }

    /// Wait until a Forget message is received with the given `peer_id`. `status` will be sent in
    /// response to the matching FIDL request.
    pub async fn expect_forget(
        &mut self,
        peer_id: String,
        mut status: FidlStatus,
    ) -> Result<(), Error> {
        expect_call(&mut self.stream, self.timeout, move |req| {
            Ok(match req {
                ControlRequest::Forget { device_id, responder } => {
                    if device_id == peer_id {
                        responder.send(&mut status)?;
                        Status::Satisfied(())
                    } else {
                        // Respond with success for the ignored call.
                        responder.send(&mut bt_fidl_status!())?;
                        Status::Pending
                    }
                }
                _ => Status::Pending,
            })
        })
        .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {fuchsia_zircon::DurationNum, futures::join};

    fn timeout() -> Duration {
        20.seconds()
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_expect_disconnect() {
        let (proxy, mut mock) = ControlMock::new(timeout()).expect("failed to create mock");
        let peer_id = "1".to_string();

        let disconnect = proxy.disconnect(&peer_id);
        let expect = mock.expect_disconnect(peer_id, bt_fidl_status!());

        let (disconnect_result, expect_result) = join!(disconnect, expect);
        let _ = disconnect_result.expect("disconnect request failed");
        let _ = expect_result.expect("expectation not satisfied");
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_expect_forget() {
        let (proxy, mut mock) = ControlMock::new(timeout()).expect("failed to create mock");
        let peer_id = "1".to_string();

        let forget = proxy.forget(&peer_id);
        let expect = mock.expect_forget(peer_id, bt_fidl_status!());

        let (forget_result, expect_result) = join!(forget, expect);
        let _ = forget_result.expect("forget request failed");
        let _ = expect_result.expect("expectation not satisifed");
    }
}
