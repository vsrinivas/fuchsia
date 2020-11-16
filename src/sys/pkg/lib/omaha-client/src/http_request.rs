// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    futures::future::BoxFuture,
    futures::prelude::*,
    hyper::{Body, Request, Response},
};

pub mod mock;

/// A trait for providing HTTP capabilities to the StateMachine.
///
/// This trait is a wrapper around Hyper, to provide a simple request->response style of API for
/// the state machine to use.
///
/// In particular, it's meant to be easy to mock for tests.
pub trait HttpRequest {
    /// Make a request, and return an Response, as the header Parts and collect the entire collected
    /// Body as a Vec of bytes.
    fn request(&mut self, req: Request<Body>) -> BoxFuture<'_, Result<Response<Vec<u8>>, Error>>;
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
    Timeout,
}

impl Error {
    /// Create a timeout error
    ///
    /// This is valid for use in tests as well as production implementations of the trait, if
    /// application-layer timeouts are being implemented.
    pub fn new_timeout() -> Self {
        Self { kind: ErrorKind::Timeout, source: None }
    }

    /// Returns true if this error the result of the Hyper API being incorrectly used (a "user"
    /// error in Hyper)
    pub fn is_user(&self) -> bool {
        self.kind == ErrorKind::User
    }

    /// Returns true if this error is the result of a timeout when trying to full-fill the request
    ///
    /// Note: Connect timeouts may be returned as io errors,  not timeouts, depending on where in
    /// the network / http client stack the timeout occurs in.
    pub fn is_timeout(&self) -> bool {
        self.kind == ErrorKind::Timeout
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
    fn request(&mut self, _req: Request<Body>) -> BoxFuture<'_, Result<Response<Vec<u8>>, Error>> {
        future::ok(Response::default()).boxed()
    }
}
