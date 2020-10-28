// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    futures::channel::mpsc::UnboundedSender,
};

/// A struct which forwards `InputDeviceRegistryRequestStream`s over an
/// `mpsc::UnboundedSender`.
pub(crate) struct InputDeviceRegistryServer {
    sender: UnboundedSender<InputDeviceRegistryRequestStream>,
}

/// Creates an `InputDeviceRegistryServer`.
///
/// Returns both the server, and the `mpsc::UnboundedReceiver` which can be
/// used to receive `InputDeviceRegistryRequestStream`'s forwarded by the server.
pub(crate) fn make_server_and_receiver() -> (
    InputDeviceRegistryServer,
    futures::channel::mpsc::UnboundedReceiver<InputDeviceRegistryRequestStream>,
) {
    let (sender, receiver) = futures::channel::mpsc::unbounded();
    (InputDeviceRegistryServer { sender }, receiver)
}

impl InputDeviceRegistryServer {
    /// Handles the incoming `InputDeviceRegistryRequestStream`.
    ///
    /// Simply forwards the stream over the `mpsc::UnboundedSender`.
    pub async fn handle_request(
        &self,
        stream: InputDeviceRegistryRequestStream,
    ) -> Result<(), Error> {
        self.sender.unbounded_send(stream).map_err(anyhow::Error::from)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_input_injection::InputDeviceRegistryMarker, fuchsia_async as fasync,
        matches::assert_matches,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_handle_request_forwards_stream_and_returns_ok() {
        let (server, mut receiver) = make_server_and_receiver();
        let (_proxy, stream) = create_proxy_and_stream::<InputDeviceRegistryMarker>()
            .expect("should make proxy/stream");
        assert_matches!(server.handle_request(stream).await, Ok(()));

        // Note: can't use `assert_matches!()` here, because `InputDeviceRegistryRequestStream`
        // does not implement `Debug`.
        match receiver.try_next() {
            Ok(opt) => assert!(opt.is_some()),
            Err(e) => panic!("reading failed with {:#?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_handle_request_returns_error_on_disconnected_receiver() {
        let (server, receiver) = make_server_and_receiver();
        let (_proxy, stream) = create_proxy_and_stream::<InputDeviceRegistryMarker>()
            .expect("should make proxy/stream");
        std::mem::drop(receiver);
        assert_matches!(server.handle_request(stream).await, Err(_));
    }
}
