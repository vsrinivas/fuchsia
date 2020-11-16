// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! These are Handler implementations that are specifically for injecting faults into the behavior
//! of the server.

use {
    crate::Handler,
    futures::{
        future::{pending, BoxFuture},
        prelude::*,
    },
    hyper::{header::CONTENT_LENGTH, Body, Request, Response, StatusCode},
};

/// Handler that never sends bytes.
pub struct Hang;

impl Handler for Hang {
    fn handles(&self, _: &Request<Body>) -> Option<BoxFuture<'_, Response<Body>>> {
        Some(pending().boxed())
    }
}

/// Handler that sends the header but then never sends body bytes.
pub struct HangBody {
    content_length: u32,
}

impl HangBody {
    /// Create a new HangBody handler which returns HTTP response headers, stating that it will
    /// return a body of the given content length, and then never returns a body (hanging).
    pub fn content_length(content_length: u32) -> Self {
        Self { content_length }
    }
}

impl Handler for HangBody {
    fn handles(&self, _: &Request<Body>) -> Option<BoxFuture<'_, Response<Body>>> {
        let content_length = self.content_length;
        Some(
            async move {
                Response::builder()
                    .status(StatusCode::OK)
                    .header(CONTENT_LENGTH, content_length)
                    .body(Body::wrap_stream(futures::stream::pending::<Result<Vec<u8>, String>>()))
                    .expect("valid response")
            }
            .boxed(),
        )
    }
}
