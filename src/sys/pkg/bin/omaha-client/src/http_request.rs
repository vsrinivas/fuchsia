// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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

        collect_from_future(response).on_timeout(self.timeout, || Err(Error::new_timeout())).boxed()
    }
}

// Helper to clarify the types of the futures involved
async fn collect_from_future(response_future: ResponseFuture) -> Result<Response<Vec<u8>>, Error> {
    let response = response_future.await.map_err(|e| Error::from(e))?;
    let (parts, body) = response.into_parts();
    let collected_body = body
        .try_fold(Vec::new(), |mut vec, b| async move {
            vec.extend(b);
            Ok(vec)
        })
        .await?;
    Ok(Response::from_parts(parts, collected_body))
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

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_hyper_test_support::{
        fault_injection::{Hang, HangBody},
        handler::StaticResponse,
        TestServer,
    };

    /// Helper that constructs a Request for a given path on the given test server.
    fn make_request_for(server: &TestServer, path: &str) -> Request<Body> {
        Request::builder().uri(server.local_url_for_path(path)).body(Body::empty()).unwrap()
    }

    /// Test that the HttpRequest implementation works against a simple server and returns
    /// the expected response body.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_simple_request() {
        let server = TestServer::builder().handler(StaticResponse::ok_body("some data")).start();
        let mut client = FuchsiaHyperHttpRequest::using_timeout(Duration::from_secs(5));
        let response = client.request(make_request_for(&server, "some/path")).await.unwrap();
        let string = String::from_utf8(response.into_body()).unwrap();
        assert_eq!(string, "some data");
    }

    /// Test that the HttpRequest implementation properly times out if the server doesn't return
    /// a response over the socket after accepting the connection.
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_hang() {
        let server = TestServer::builder().handler(Hang).start();
        let mut client = FuchsiaHyperHttpRequest::using_timeout(Duration::from_secs(1));
        let response = client.request(make_request_for(&server, "some/path")).await;
        assert!(response.unwrap_err().is_timeout());
    }

    /// Test that the HttpRequest implementation properly times out if the server doesn't return
    /// a the entire body that's expected (after returning a response header).
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_hang_body() {
        let server = TestServer::builder().handler(HangBody::content_length(500)).start();
        let mut client = FuchsiaHyperHttpRequest::using_timeout(Duration::from_secs(1));
        let response = client.request(make_request_for(&server, "some/path")).await;
        assert!(response.unwrap_err().is_timeout());
    }
}
