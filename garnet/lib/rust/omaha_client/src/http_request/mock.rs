// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::HttpRequest;
use futures::compat::Stream01CompatExt;
use futures::executor::block_on;
use futures::future::FutureObj;
use futures::prelude::*;
use hyper::{Body, Request, Response};

pub struct MockHttpRequest {
    // The last request made using this mock.
    request: Request<Body>,
    // The fake response for the next request, only valid for one request.
    response: Option<Response<Body>>,
}

impl HttpRequest for MockHttpRequest {
    fn request(&mut self, req: Request<Body>) -> FutureObj<Result<Response<Body>, hyper::Error>> {
        self.request = req;

        let resp = self.response.take().expect("no response to return");
        return FutureObj::new(Box::pin(future::ready(Ok(resp))));
    }
}

impl MockHttpRequest {
    pub fn new(res: Response<Body>) -> MockHttpRequest {
        MockHttpRequest { request: Request::default(), response: Some(res) }
    }

    pub fn set_response(&mut self, res: Response<Body>) {
        self.response = Some(res);
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
        let chunks = await!(self.request.into_body().compat().try_concat()).unwrap();
        assert_eq!(body, chunks.as_ref())
    }
}

async fn response_to_vec(response: Response<Body>) -> Vec<u8> {
    await!(response.into_body().compat().try_concat()).unwrap().to_vec()
}

#[test]
fn test_mock() {
    let res_body = vec![1, 2, 3];
    let mut mock = MockHttpRequest::new(Response::new(res_body.clone().into()));

    let req_body = vec![4, 5, 6];
    let uri = "https://mock.uri/";
    let req =
        Request::get(uri).header("X-Custom-Foo", "Bar").body(req_body.clone().into()).unwrap();
    block_on(
        async {
            let response = await!(mock.request(req)).unwrap();
            assert_eq!(res_body, await!(response_to_vec(response)));

            mock.assert_method(&hyper::Method::GET);
            mock.assert_uri(uri);
            mock.assert_header("X-Custom-Foo", "Bar");
            await!(mock.assert_body(req_body.as_slice()));
        },
    );
}

#[test]
#[should_panic(expected = "no response to return")]
fn test_missing_response() {
    let res_body = vec![1, 2, 3];
    let mut mock = MockHttpRequest::new(Response::new(res_body.clone().into()));
    block_on(
        async {
            let response = await!(mock.request(Request::default())).unwrap();
            assert_eq!(res_body, await!(response_to_vec(response)));

            let _response2 = await!(mock.request(Request::default()));
        },
    );
}

#[test]
fn test_set_response() {
    let res_body = vec![1, 2, 3];
    let mut mock = MockHttpRequest::new(Response::new(res_body.clone().into()));
    block_on(
        async {
            let response = await!(mock.request(Request::default())).unwrap();
            assert_eq!(res_body, await!(response_to_vec(response)));

            let res_body2 = vec![4, 5, 6];
            mock.set_response(Response::new(res_body2.clone().into()));
            let response2 = await!(mock.request(Request::default())).unwrap();
            assert_eq!(res_body2, await!(response_to_vec(response2)));
        },
    );
}
