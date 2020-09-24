// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::directory_request_logger::DirectoryRequestLogger, fidl_fuchsia_io as io,
    fuchsia_async as fasync, futures::channel::mpsc,
};

/// In-progress impl to use io1 server requests to get back an interposed server request
/// to allow for calls on the channel to be logged into the receiver.
/// TODO(fxbug.dev/45617): In the future we might want to make this protocol specific.
#[derive(Clone)]
pub struct Io1RequestLoggerFactory {
    tx: mpsc::Sender<String>,
}

impl Io1RequestLoggerFactory {
    /// Returns an Io1RequestLoggerFactory and a channel to listen for all requests
    /// that go through Loggers instantiated by this factory.
    pub fn new() -> (Self, mpsc::Receiver<String>) {
        let (tx, rx) = mpsc::channel(0);
        (Io1RequestLoggerFactory { tx }, rx)
    }

    /// Creates a DirectoryRequestLogger that listens for requests on the passed
    /// in server end channel to log through the factory. Returns the
    /// DirectoryRequestLogger's own server end channel to be able to forward
    /// requests to the actual server.
    pub async fn get_logged_directory(
        &self,
        name: String,
        server_end: fidl::endpoints::ServerEnd<io::DirectoryMarker>,
    ) -> fidl::endpoints::ServerEnd<io::DirectoryMarker> {
        let tx = self.tx.clone();

        let mut logger = DirectoryRequestLogger::new(name, tx);
        let (logged_client, logged_server) = fidl::endpoints::create_proxy::<io::DirectoryMarker>()
            .expect("unable to create endpoints");

        fasync::Task::spawn(async move {
            let stream =
                server_end.into_stream().expect("Could not convert directory request to stream.");
            logger.log_requests(stream, logged_client).await;
        })
        .detach();

        logged_server
    }
}
