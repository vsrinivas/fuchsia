// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
// Copied from //src/sys/pkg/bin/omaha-client.

#![allow(dead_code)]

use {
    fuchsia_async::TimeoutExt,
    futures::{future::BoxFuture, prelude::*},
    hyper::{client::ResponseFuture, Body, Client, Request, Response},
    omaha_client::http_request::{Error, HttpRequest},
    std::time::Duration,
};

pub struct FuchsiaHyperHttpRequest {
    timeout: Duration,
    client: Client<hyper_rustls::HttpsConnector<fuchsia_hyper::HyperConnector>, Body>,
}

impl HttpRequest for FuchsiaHyperHttpRequest {
    fn request(&mut self, req: Request<Body>) -> BoxFuture<'_, Result<Response<Vec<u8>>, Error>> {
        // create the initial response future
        let response = self.client.request(req);
        let timeout = self.timeout;

        collect_from_future(response).on_timeout(timeout, || Err(Error::new_timeout())).boxed()
    }
}

// Helper to clarify the types of the futures involved
async fn collect_from_future(response_future: ResponseFuture) -> Result<Response<Vec<u8>>, Error> {
    let response = response_future.await.map_err(Error::from)?;
    let (parts, body) = response.into_parts();
    let bytes = hyper::body::to_bytes(body).await?;
    Ok(Response::from_parts(parts, bytes.to_vec()))
}

impl FuchsiaHyperHttpRequest {
    /// Construct a new client that uses a default timeout.
    pub fn new() -> Self {
        Self::using_timeout(Duration::from_secs(30))
    }

    /// Construct a new client which always uses the provided duration instead of the default
    pub fn using_timeout(timeout: Duration) -> Self {
        FuchsiaHyperHttpRequest { timeout, client: fuchsia_hyper::new_https_client() }
    }
}
