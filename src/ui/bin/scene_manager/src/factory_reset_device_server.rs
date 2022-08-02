// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_recovery_policy::DeviceRequestStream,
    futures::channel::mpsc::UnboundedSender,
};

/// A struct which forwards `DeviceRequestStream`s over an
/// `mpsc::UnboundedSender`.
pub(crate) struct RecoveryPolicyDeviceServer {
    sender: UnboundedSender<DeviceRequestStream>,
}

/// Creates an `RecoveryPolicyDeviceServer`.
///
/// Returns both the server, and the `mpsc::UnboundedReceiver` which can be
/// used to receive `DeviceRequest`'s forwarded by the server.
pub(crate) fn make_server_and_receiver(
) -> (RecoveryPolicyDeviceServer, futures::channel::mpsc::UnboundedReceiver<DeviceRequestStream>) {
    let (sender, receiver) = futures::channel::mpsc::unbounded::<DeviceRequestStream>();
    (RecoveryPolicyDeviceServer { sender }, receiver)
}

impl RecoveryPolicyDeviceServer {
    /// Handles the incoming `DeviceRequest`.
    ///
    /// Simply forwards the request over the `mpsc::UnboundedSender`.
    pub async fn handle_request(&self, stream: DeviceRequestStream) -> Result<(), Error> {
        self.sender.unbounded_send(stream).map_err(anyhow::Error::from)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, assert_matches::assert_matches, fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_recovery_policy::DeviceMarker,
    };

    #[fuchsia::test(allow_stalls = false)]
    async fn test_handle_request_forwards_stream_and_returns_ok() {
        let (server, mut receiver) = make_server_and_receiver();
        let (_proxy, stream) =
            create_proxy_and_stream::<DeviceMarker>().expect("should make proxy/stream");
        assert_matches!(server.handle_request(stream).await, Ok(()));
        assert!(receiver.try_next().expect("should return ok").is_some());
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_handle_request_returns_error_on_disconnected_receiver() {
        let (server, receiver) = make_server_and_receiver();
        let (_proxy, stream) =
            create_proxy_and_stream::<DeviceMarker>().expect("should make proxy/stream");
        std::mem::drop(receiver);
        assert_matches!(server.handle_request(stream).await, Err(_));
    }
}
