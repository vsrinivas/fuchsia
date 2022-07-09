// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::prelude::*;
use std::pin::Pin;
use std::task::{Context, Poll};

pub struct ReportSkipped<T>(T);

impl<T> ReportSkipped<T> {
    pub fn new(tag: T) -> Self {
        Self(tag)
    }
}

impl<T: std::fmt::Display> AsyncWrite for ReportSkipped<T> {
    fn poll_write(
        self: Pin<&mut Self>,
        _ctx: &mut Context<'_>,
        bytes: &[u8],
    ) -> Poll<Result<usize, std::io::Error>> {
        tracing::info!("{}: {:?} {:?}", self.0, bytes, std::str::from_utf8(bytes));
        Poll::Ready(Ok(bytes.len()))
    }

    fn poll_flush(
        self: Pin<&mut Self>,
        _ctx: &mut Context<'_>,
    ) -> Poll<Result<(), std::io::Error>> {
        Poll::Ready(Ok(()))
    }

    fn poll_close(
        self: Pin<&mut Self>,
        _ctx: &mut Context<'_>,
    ) -> Poll<Result<(), std::io::Error>> {
        Poll::Ready(Ok(()))
    }
}
