// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::http_request::{Error, HttpRequest};
use futures::future::BoxFuture;
use futures::prelude::*;
use http::StatusCode;
use hyper::{Body, Request, Response};
use pretty_assertions::assert_eq;
use std::{cell::RefCell, collections::VecDeque, rc::Rc};

#[cfg(test)]
use futures::executor::block_on;

#[derive(Debug, Default)]
pub struct MockHttpRequest {
    // The last request made using this mock.
    request: Rc<RefCell<Request<Body>>>,
    // The queue of fake responses for the upcoming requests.
    responses: VecDeque<Result<Response<Vec<u8>>, Error>>,
}

impl HttpRequest for MockHttpRequest {
    fn request(&mut self, req: Request<Body>) -> BoxFuture<'_, Result<Response<Vec<u8>>, Error>> {
        self.request.replace(req);

        future::ready(if let Some(resp) = self.responses.pop_front() {
            resp
        } else {
            // No response to return, generate a 500 internal server error
            Ok(Response::builder().status(StatusCode::INTERNAL_SERVER_ERROR).body(vec![]).unwrap())
        })
        .boxed()
    }
}

impl MockHttpRequest {
    pub fn new(res: Response<Vec<u8>>) -> Self {
        Self { responses: vec![Ok(res)].into(), ..Default::default() }
    }

    pub fn empty() -> Self {
        Default::default()
    }

    pub fn from_request_cell(request: Rc<RefCell<Request<Body>>>) -> Self {
        Self { request, ..Default::default() }
    }

    pub fn get_request_cell(&self) -> Rc<RefCell<Request<Body>>> {
        Rc::clone(&self.request)
    }

    pub fn add_response(&mut self, res: Response<Vec<u8>>) {
        self.responses.push_back(Ok(res));
    }

    pub fn add_error(&mut self, error: Error) {
        self.responses.push_back(Err(error));
    }

    pub fn assert_method(&self, method: &hyper::Method) {
        assert_eq!(method, self.request.borrow().method());
    }

    pub fn assert_uri(&self, uri: &str) {
        assert_eq!(&uri.parse::<hyper::Uri>().unwrap(), self.request.borrow().uri());
    }

    pub fn assert_header(&self, key: &str, value: &str) {
        let request = self.request.borrow();
        let headers = request.headers();
        assert!(headers.contains_key(key));
        assert_eq!(headers[key], value);
    }

    fn take_request(&self) -> Request<Body> {
        self.request.replace(Request::default())
    }

    pub async fn assert_body(self, body: &[u8]) {
        let chunks = self
            .take_request()
            .into_body()
            .try_fold(Vec::new(), |mut vec, b| async move {
                vec.extend(b);
                Ok(vec)
            })
            .await
            .unwrap();
        assert_eq!(body, &chunks[..])
    }

    pub async fn assert_body_str(self, body: &str) {
        let chunks = self
            .take_request()
            .into_body()
            .try_fold(Vec::new(), |mut vec, b| async move {
                vec.extend(b);
                Ok(vec)
            })
            .await
            .unwrap();
        assert_eq!(body, String::from_utf8_lossy(chunks.as_ref()));
    }
}

#[test]
fn test_mock() {
    let res_body = vec![1, 2, 3];
    let mut mock = MockHttpRequest::new(Response::new(res_body.clone()));

    let req_body = vec![4, 5, 6];
    let uri = "https://mock.uri/";
    let req =
        Request::get(uri).header("X-Custom-Foo", "Bar").body(req_body.clone().into()).unwrap();
    block_on(async {
        let response = mock.request(req).await.unwrap();
        assert_eq!(res_body, response.into_body());

        mock.assert_method(&hyper::Method::GET);
        mock.assert_uri(uri);
        mock.assert_header("X-Custom-Foo", "Bar");
        mock.assert_body(req_body.as_slice()).await;
    });
}

#[test]
fn test_missing_response() {
    let res_body = vec![1, 2, 3];
    let mut mock = MockHttpRequest::new(Response::new(res_body.clone()));
    block_on(async {
        let response = mock.request(Request::default()).await.unwrap();
        assert_eq!(res_body, response.into_body());

        let response2 = mock.request(Request::default()).await.unwrap();
        assert_eq!(response2.status(), hyper::StatusCode::INTERNAL_SERVER_ERROR);
    });
}

#[test]
fn test_multiple_responses() {
    let res_body = vec![1, 2, 3];
    let mut mock = MockHttpRequest::new(Response::new(res_body.clone()));
    let res_body2 = vec![4, 5, 6];
    mock.add_response(Response::new(res_body2.clone()));

    block_on(async {
        let response = mock.request(Request::default()).await.unwrap();
        assert_eq!(res_body, response.into_body());

        let response2 = mock.request(Request::default()).await.unwrap();
        assert_eq!(res_body2, response2.into_body());
    });
}
