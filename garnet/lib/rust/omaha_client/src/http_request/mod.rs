// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::FutureObj;
use hyper::{Body, Request, Response};

pub trait HttpRequest {
    fn request(&mut self, req: Request<Body>) -> FutureObj<Result<Response<Body>, hyper::Error>>;
}

#[cfg(target_os = "fuchsia")]
pub mod fuchsia_hyper;

#[cfg(test)]
pub mod mock;
