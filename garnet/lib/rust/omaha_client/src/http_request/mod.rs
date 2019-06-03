// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::BoxFuture;
use hyper::{Body, Request, Response};

pub trait HttpRequest {
    fn request(&mut self, req: Request<Body>) -> BoxFuture<Result<Response<Body>, hyper::Error>>;
}

#[cfg(test)]
pub mod mock;
