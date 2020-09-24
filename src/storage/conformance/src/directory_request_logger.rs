// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io as io,
    futures::{channel::mpsc, sink::SinkExt, StreamExt},
};

/// Allows logging the requests of one Directory client-server pair.
#[derive(Clone)]
pub struct DirectoryRequestLogger {
    name: String,
    tx: mpsc::Sender<String>,
}

impl DirectoryRequestLogger {
    /// Returns a DirectoryRequestLogger that sends messages on the passed-in
    /// channel |tx| during log_requests annotated with the |name| of the logger.
    pub fn new(name: String, tx: mpsc::Sender<String>) -> Self {
        DirectoryRequestLogger { name, tx }
    }

    /// Forwards requests to the server end of the connection and also sends
    /// a string message about the request to the tx log channel.
    /// TODO(fxbug.dev/45613): Currently we are only supporting forwarding Directory.Open calls.
    /// We need a better generic way to forward requests. Also, the logged open request
    /// format is a custom string. This could be made into a better format as well.
    pub async fn log_requests(
        &mut self,
        mut from_client: io::DirectoryRequestStream,
        to_service: io::DirectoryProxy,
    ) {
        while let Some(Ok(request)) = from_client.next().await {
            match request {
                io::DirectoryRequest::Open { flags, mode, path, object, control_handle: _ } => {
                    self.tx
                        .send(format!(
                            "{} flags:{}, mode:{}, path:{}",
                            self.name, flags, mode, path
                        ))
                        .await
                        .expect(&format!("Directory {} tx/rx/channel was closed", self.name));
                    to_service
                        .open(flags, mode, &path, object)
                        .expect("Directory open call failed.");
                }
                _ => (),
            }
        }
    }
}
