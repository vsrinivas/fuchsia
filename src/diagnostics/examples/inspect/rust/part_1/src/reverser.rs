// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_examples_inspect::{ReverserRequest, ReverserRequestStream},
    fuchsia_async as fasync,
    fuchsia_zircon::DurationNum,
    futures::TryStreamExt,
};

// [START reverser_def]
pub struct ReverserServerFactory {}

impl ReverserServerFactory {
    // CODELAB: Create a new() constructor that takes an Inspect node.
    pub fn new() -> Self {
        Self {}
    }

    pub fn spawn_new(&self, stream: ReverserRequestStream) {
        // CODELAB: Add stats about incoming connections.
        ReverserServer::new().spawn(stream);
    }
}

struct ReverserServer {}

impl ReverserServer {
    // CODELAB: Create a new() constructor that takes an Inspect node.
    fn new() -> Self {
        Self {}
    }

    pub fn spawn(self, mut stream: ReverserRequestStream) {
        fasync::Task::local(async move {
            while let Some(request) = stream.try_next().await.expect("serve reverser") {
                // CODELAB: Add stats about incoming requests.
                let ReverserRequest::Reverse { input, responder: _ } = request;
                let _result = input.chars().rev().collect::<String>();
                // Yes, this is silly. Just for codelab purposes.
                fasync::Timer::new(fasync::Time::after(10.hours())).await
            }
        })
        .detach();
    }
}
// [END reverser_def]

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Error,
        fidl_fuchsia_examples_inspect::{ReverserMarker, ReverserProxy},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_reverser() -> Result<(), Error> {
        let _reverser = open_reverser()?;
        // CODELAB: Test the response from the reverser.
        // let result = reverser.reverse("hello").await?;
        // assert_eq!(result, "olleh");
        Ok(())
    }

    fn open_reverser() -> Result<ReverserProxy, Error> {
        let (proxy, _stream) = fidl::endpoints::create_proxy_and_stream::<ReverserMarker>()?;
        let _reverser = ReverserServer::new();
        // CODELAB: Uncomment this line to return a real reverser connection.
        // reverser.spawn(stream);
        Ok(proxy)
    }
}
