// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::InterfaceFactory,
    crate::target::Target,
    anyhow::Result,
    async_trait::async_trait,
    futures::{
        io::{AsyncRead, AsyncWrite},
        task::{Context, Poll},
    },
    std::pin::Pin,
};

pub(crate) struct NetworkInterface {}

impl AsyncRead for NetworkInterface {
    fn poll_read(
        self: Pin<&mut Self>,
        _cx: &mut Context<'_>,
        _buf: &mut [u8],
    ) -> Poll<std::io::Result<usize>> {
        unimplemented!();
    }
}

impl AsyncWrite for NetworkInterface {
    fn poll_write(
        #[allow(unused_mut)] mut self: Pin<&mut Self>,
        _cx: &mut Context<'_>,
        _buf: &[u8],
    ) -> Poll<std::io::Result<usize>> {
        unimplemented!();
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
        unimplemented!();
    }

    fn poll_close(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
        unimplemented!();
    }
}

pub(crate) struct NetworkFactory {}

impl NetworkFactory {
    #[allow(dead_code)]
    pub(crate) fn new() -> Self {
        Self {}
    }
}

#[async_trait(?Send)]
impl InterfaceFactory<NetworkInterface> for NetworkFactory {
    async fn open(&mut self, _target: &Target) -> Result<NetworkInterface> {
        unimplemented!();
    }

    async fn close(&self) {
        unimplemented!();
    }
}
