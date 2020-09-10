// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::future::BoxFuture;
use futures::prelude::*;
use hyper::{Body, Request, Response};

pub mod mock;

pub trait HttpRequest {
    fn request(&mut self, req: Request<Body>) -> BoxFuture<'_, Result<Response<Body>, Error>>;
}

#[derive(Debug, thiserror::Error)]
// Parentheses are needed for .source, but will trigger unused_parens, so a tuple is used.
#[error("Http request failed: {}", match (.source, ()).0 {
    Some(source) => format!("{}", source),
    None => format!("kind: {:?}", .kind),
})]
pub struct Error {
    kind: ErrorKind,
    #[source]
    source: Option<hyper::Error>,
}

#[derive(Debug, Eq, PartialEq)]
enum ErrorKind {
    User,
    Transport,
}

impl Error {
    pub fn is_user(&self) -> bool {
        self.kind == ErrorKind::User
    }
}

impl From<hyper::Error> for Error {
    fn from(error: hyper::Error) -> Self {
        let kind = if error.is_user() { ErrorKind::User } else { ErrorKind::Transport };
        Error { kind, source: error.into() }
    }
}

pub mod mock_errors {
    use super::*;

    pub fn make_user_error() -> Error {
        Error { kind: ErrorKind::User, source: None }
    }

    pub fn make_transport_error() -> Error {
        Error { kind: ErrorKind::Transport, source: None }
    }
}

/// A stub HttpRequest that does nothing and returns an empty response immediately.
pub struct StubHttpRequest;

impl HttpRequest for StubHttpRequest {
    fn request(&mut self, _req: Request<Body>) -> BoxFuture<'_, Result<Response<Body>, Error>> {
        future::ok(Response::new(Body::empty())).boxed()
    }
}
