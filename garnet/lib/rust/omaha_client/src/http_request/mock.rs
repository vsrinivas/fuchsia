// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::http_request::HttpRequest;
use futures::compat::Stream01CompatExt;
use futures::executor::block_on;
use futures::future::BoxFuture;
use futures::prelude::*;
use hyper::{Body, Request, Response};
use pretty_assertions::assert_eq;
use std::collections::VecDeque;

#[derive(Debug)]
pub struct MockHttpRequest {
    // The last request made using this mock.
    request: Request<Body>,
    // The queue of fake responses for the upcoming requests.
    responses: VecDeque<Response<Body>>,
}

impl HttpRequest for MockHttpRequest {
    fn request(&mut self, req: Request<Body>) -> BoxFuture<'_, Result<Response<Body>, hyper::Error>> {
        self.request = req;

        future::ok(if let Some(resp) = self.responses.pop_front() {
            resp
        } else {
            // No response to return, generate a 500 internal server error
            Response::builder().status(500).body(Body::empty()).unwrap()
        })
        .boxed()
    }
}

impl MockHttpRequest {
    pub fn new(res: Response<Body>) -> MockHttpRequest {
        MockHttpRequest { request: Request::default(), responses: vec![res].into() }
    }

    pub fn empty() -> MockHttpRequest {
        MockHttpRequest { request: Request::default(), responses: vec![].into() }
    }

    pub fn add_response(&mut self, res: Response<Body>) {
        self.responses.push_back(res);
    }

    pub fn assert_method(&self, method: &hyper::Method) {
        assert_eq!(method, self.request.method());
    }

    pub fn assert_uri(&self, uri: &str) {
        assert_eq!(&uri.parse::<hyper::Uri>().unwrap(), self.request.uri());
    }

    pub fn assert_header(&self, key: &str, value: &str) {
        let headers = self.request.headers();
        assert!(headers.contains_key(key));
        assert_eq!(headers[key], value);
    }

    pub async fn assert_body(self, body: &[u8]) {
        let chunks = self.request.into_body().compat().try_concat().await.unwrap();
        assert_eq!(body, chunks.as_ref())
    }

    pub async fn assert_body_str(self, body: &str) {
        let chunks = self.request.into_body().compat().try_concat().await.unwrap();
        assert_eq!(body, String::from_utf8_lossy(chunks.as_ref()));
    }
}

async fn response_to_vec(response: Response<Body>) -> Vec<u8> {
    response.into_body().compat().try_concat().await.unwrap().to_vec()
}

#[test]
fn test_mock() {
    let res_body = vec![1, 2, 3];
    let mut mock = MockHttpRequest::new(Response::new(res_body.clone().into()));

    let req_body = vec![4, 5, 6];
    let uri = "https://mock.uri/";
    let req =
        Request::get(uri).header("X-Custom-Foo", "Bar").body(req_body.clone().into()).unwrap();
    block_on(async {
        let response = mock.request(req).await.unwrap();
        assert_eq!(res_body, response_to_vec(response).await);

        mock.assert_method(&hyper::Method::GET);
        mock.assert_uri(uri);
        mock.assert_header("X-Custom-Foo", "Bar");
        mock.assert_body(req_body.as_slice()).await;
    });
}

#[test]
fn test_missing_response() {
    let res_body = vec![1, 2, 3];
    let mut mock = MockHttpRequest::new(Response::new(res_body.clone().into()));
    block_on(async {
        let response = mock.request(Request::default()).await.unwrap();
        assert_eq!(res_body, response_to_vec(response).await);

        let response2 = mock.request(Request::default()).await.unwrap();
        assert_eq!(response2.status(), hyper::StatusCode::INTERNAL_SERVER_ERROR);
    });
}

#[test]
fn test_multiple_responses() {
    let res_body = vec![1, 2, 3];
    let mut mock = MockHttpRequest::new(Response::new(res_body.clone().into()));
    let res_body2 = vec![4, 5, 6];
    mock.add_response(Response::new(res_body2.clone().into()));

    block_on(async {
        let response = mock.request(Request::default()).await.unwrap();
        assert_eq!(res_body, response_to_vec(response).await);

        let response2 = mock.request(Request::default()).await.unwrap();
        assert_eq!(res_body2, response_to_vec(response2).await);
    });
}
