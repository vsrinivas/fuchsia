// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::expect::{expect_call, Status},
    anyhow::Error,
    fidl_fuchsia_bluetooth::{PeerId as FidlPeerId, Status as FidlStatus},
    fidl_fuchsia_bluetooth_control::{
        ControlMarker, ControlProxy, ControlRequest, ControlRequestStream, PairingOptions,
    },
    fuchsia_bluetooth::bt_fidl_status,
    fuchsia_zircon::Duration,
};

/// Provides a simple mock implementation of `fuchsia.bluetooth.control.Control`.
pub struct ControlMock {
    stream: ControlRequestStream,
    timeout: Duration,
}

/// Provides a way to generate simple FIDL responses to specific Control Requests. |control_req|
/// must be the name of a variant of ControlRequest. |req_param_name| must be the name of a field
/// in |control_req|. Each |req_param_name| must have a matching |expected_req_param| parameter.
/// The future the macro returns will yield Status::Satisfied once the Mock receives a FIDL call
/// of |control_req| where all the fields of |control_req| are equal to |expected_req_param|s.
macro_rules! expect_control_req {
    ($self:ident, $fidl_status:expr, $enum_name:ident::$control_req:ident, $(($req_param_name:tt, $expected_req_param:expr)),*) => {
        expect_call(&mut $self.stream, $self.timeout, move |req| {
            Ok(if let $enum_name::$control_req { $($req_param_name,)* responder } = req {
                    // The true at the end of this is a hacky way to allow us to check an arbitrary number
                    // of req_param_names
                    if $($req_param_name == $expected_req_param && )* true {
                        responder.send(&mut $fidl_status)?;
                        Status::Satisfied(())
                    }
                    else {
                        // Respond with success for an ignored call
                        responder.send(&mut bt_fidl_status!())?;
                        Status::Pending
                    }
                }
                else { Status::Pending }
            )
        })
    };
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
        expect_control_req!(self, status, ControlRequest::Disconnect, (device_id, peer_id)).await
    }

    /// Wait until a Forget message is received with the given `peer_id`. `status` will be sent in
    /// response to the matching FIDL request.
    pub async fn expect_forget(
        &mut self,
        peer_id: String,
        mut status: FidlStatus,
    ) -> Result<(), Error> {
        expect_control_req!(self, status, ControlRequest::Forget, (device_id, peer_id)).await
    }

    /// Wait until a Pair message is received with the `pairing_params`. `status` will be sent in
    /// response to the matching FIDL request
    pub async fn expect_pair(
        &mut self,
        expected_peer_id: FidlPeerId,
        expected_options: PairingOptions,
        mut status: FidlStatus,
    ) -> Result<(), Error> {
        expect_control_req!(
            self,
            status,
            ControlRequest::Pair,
            (id, expected_peer_id),
            (options, expected_options)
        )
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
