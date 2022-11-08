// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Mock HttpsClient for use in testing.
//!
//! Usage:
//! ```
//! let mut https_client = HttpsClient::mock();
//! // Set up expected requests and responses (as many as needed, in the order
//! // they should happen).
//! https_client.expect(req, res);
//! // Use the https_client in-place of the normal https_client. As matching
//! // requests come in, the premade responses will be returned.
//! ```

use {
    hyper::{Body, Request},
    std::{collections::VecDeque, sync::Mutex},
};

#[derive(Debug)]
struct HttpsClientEvent {
    req: Request<Body>,
    res: http::Result<http::Response<Body>>,
}

#[derive(Debug)]
pub struct HttpsClient {
    expected: Mutex<VecDeque<HttpsClientEvent>>,
}

impl HttpsClient {
    /// Create a new mock https client.
    ///
    /// Consider adding expected events with `.expect(req, res)`.
    pub fn mock() -> Self {
        Self { expected: Mutex::new(VecDeque::new()) }
    }

    /// Append an expected request and response.
    ///
    /// Note that `res` is actually a Result<> type.
    pub fn expect(&mut self, req: Request<Body>, res: http::Result<http::Response<Body>>) {
        self.expected.lock().expect("locking").push_back(HttpsClientEvent { req, res });
    }

    pub async fn request(&self, req: Request<Body>) -> http::Result<http::Response<Body>> {
        let expected = self.expected.lock().expect("locking").pop_front().expect(&format!(
            "Error: received more https requests than expected. \
            No response available for req: {:?}",
            req
        ));

        assert_eq!(
            expected.req.uri(),
            req.uri(),
            "mock_https_client actual {:?}, expected {:?}, related response {:?}",
            req,
            expected.req,
            expected.res
        );
        assert_eq!(expected.req.method(), req.method());
        let expected_bytes =
            hyper::body::to_bytes(expected.req.into_body()).await.expect("expected.req.into_body");
        let req_bytes = hyper::body::to_bytes(req.into_body()).await.expect("req.into_body");
        assert_eq!(expected_bytes, req_bytes);

        expected.res
    }
}

pub fn new_https_client() -> HttpsClient {
    HttpsClient::mock()
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        hyper::{Body, Method, Request},
    };

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mock() {
        let mut https_client = HttpsClient::mock();

        let req = Request::builder()
            .method(Method::POST)
            .uri("https://example.com")
            .body(Body::from("alpha"))
            .expect("Request::builder");
        let builder = http::Response::builder().status(http::StatusCode::OK);
        let res = builder.body(Body::from("beta"));
        https_client.expect(req, res);

        let req = Request::builder()
            .method(Method::POST)
            .uri("https://example.com")
            .body(Body::from("alpha"))
            .expect("Request::builder");
        let res = https_client.request(req).await.expect("https_client.request");
        assert_eq!(res.status(), http::StatusCode::OK);
        let bytes = hyper::body::to_bytes(res.into_body()).await.expect("body::to_bytes");
        assert_eq!(b"beta", &bytes[..]);
    }

    #[should_panic(expected = "actual Request")]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mock_fail() {
        let mut https_client = HttpsClient::mock();

        let req = Request::builder()
            .method(Method::POST)
            .uri("https://example.com")
            .body(Body::from("alpha"))
            .expect("Request::builder");
        let builder = http::Response::builder().status(http::StatusCode::OK);
        let res = builder.body(Body::from("beta"));
        https_client.expect(req, res);

        let req = Request::builder()
            .method(Method::POST)
            .uri("https://not-same.com")
            .body(Body::from("alpha"))
            .expect("Request::builder");
        let res = https_client.request(req).await.expect("https_client.request");
        assert_eq!(res.status(), http::StatusCode::OK);
    }

    #[test]
    fn test_new_https_client() {
        let https_client = new_https_client();
        assert!(https_client.expected.lock().expect("locking").is_empty());
    }
}
