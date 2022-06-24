// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl_fuchsia_ui_input_config::FeaturesRequestStream,
    futures::channel::mpsc::UnboundedSender,
};

/// A struct which forwards `FeaturesRequestStream`s over an
/// `mpsc::UnboundedSender`.
pub(crate) struct InputConfigFeaturesServer {
    sender: UnboundedSender<FeaturesRequestStream>,
}

/// Creates an `InputConfigFeaturesServer`.
///
/// Returns both the server, and the `mpsc::UnboundedReceiver` which can be
/// used to receive `FeaturesRequest`'s forwarded by the server.
pub(crate) fn make_server_and_receiver(
) -> (InputConfigFeaturesServer, futures::channel::mpsc::UnboundedReceiver<FeaturesRequestStream>) {
    let (sender, receiver) = futures::channel::mpsc::unbounded::<FeaturesRequestStream>();
    (InputConfigFeaturesServer { sender }, receiver)
}

impl InputConfigFeaturesServer {
    /// Handles the incoming `FeaturesRequest`.
    ///
    /// Simply forwards the request over the `mpsc::UnboundedSender`.
    pub async fn handle_request(&self, stream: FeaturesRequestStream) -> Result<(), Error> {
        self.sender.unbounded_send(stream).map_err(anyhow::Error::from)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, assert_matches::assert_matches, fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_ui_input_config::FeaturesMarker, fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_handle_request_forwards_stream_and_returns_ok() {
        let (server, mut receiver) = make_server_and_receiver();
        let (_proxy, stream) =
            create_proxy_and_stream::<FeaturesMarker>().expect("should make proxy/stream");
        assert_matches!(server.handle_request(stream).await, Ok(()));

        // Note: can't use `assert_matches!()` here, because `FeaturesRequest`
        // does not implement `Debug`.
        match receiver.try_next() {
            Ok(opt) => assert!(opt.is_some()),
            Err(e) => panic!("reading failed with {:#?}", e),
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_handle_request_returns_error_on_disconnected_receiver() {
        let (server, receiver) = make_server_and_receiver();
        let (_proxy, stream) =
            create_proxy_and_stream::<FeaturesMarker>().expect("should make proxy/stream");
        std::mem::drop(receiver);
        assert_matches!(server.handle_request(stream).await, Err(_));
    }
}
