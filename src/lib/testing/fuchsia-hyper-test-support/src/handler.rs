// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Handler implementations

use {
    crate::Handler,
    futures::{
        future::{ready, BoxFuture},
        prelude::*,
    },
    hyper::{Body, Request, Response, StatusCode},
    std::path::PathBuf,
};

/// Returns a fixed response for any request (it doesn't match on any path)
#[derive(Default)]
pub struct StaticResponse {
    status: StatusCode,
    headers: Vec<(String, String)>,
    body: Vec<u8>,
}
impl Handler for StaticResponse {
    fn handles(&self, _: &Request<Body>) -> Option<BoxFuture<'_, Response<Body>>> {
        let mut builder = Response::builder();
        builder = builder.status(self.status);
        for (key, value) in &self.headers {
            builder = builder.header(key, value);
        }
        return builder.body(self.body.clone().into()).ok().map(|r| ready(r).boxed());
    }
}
impl StaticResponse {
    /// Create a new StaticResponse handler, which returns the given response body.
    pub fn ok_body(body: impl Into<Vec<u8>>) -> Self {
        StaticResponse { status: StatusCode::OK, headers: vec![], body: body.into() }
    }
}

/// Handler wrapper that responds to the given request path using the given handler.
pub struct ForPath<H> {
    path: PathBuf,
    handler: H,
}
impl<H> ForPath<H> {
    /// Create a new ForPath handler for the given path and composed Handler.
    pub fn new(path: impl Into<PathBuf>, handler: H) -> Self {
        Self { path: path.into(), handler }
    }
}

impl<H> Handler for ForPath<H>
where
    H: Handler,
{
    fn handles(&self, request: &Request<Body>) -> Option<BoxFuture<'_, Response<Body>>> {
        if self.path == PathBuf::from(request.uri().path()) {
            self.handler.handles(request)
        } else {
            None
        }
    }
}
