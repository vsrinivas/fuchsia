// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::BoxFuture;
use futures::prelude::*;
use hyper::{Body, Request, Response};

pub trait HttpRequest {
    fn request(&mut self, req: Request<Body>) -> BoxFuture<'_, Result<Response<Body>, hyper::Error>>;
}

#[cfg(test)]
pub mod mock;

/// A stub HttpRequest that does nothing and returns an empty response immediately.
pub struct StubHttpRequest;

impl HttpRequest for StubHttpRequest {
    fn request(&mut self, _req: Request<Body>) -> BoxFuture<'_, Result<Response<Body>, hyper::Error>> {
        future::ok(Response::new(Body::empty())).boxed()
    }
}
