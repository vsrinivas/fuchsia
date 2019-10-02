// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(target_os = "fuchsia")]

//! Epitaph support for zx::Channel and fasync::Channel.

use {
    crate::{
        encoding::{self, EpitaphBody, TransactionHeader, TransactionMessage},
        error::Error,
    },
    fuchsia_async as fasync, fuchsia_zircon as zx,
};

/// Extension trait that provides Channel-like objects with the ability to send a FIDL epitaph.
pub trait ChannelEpitaphExt {
    /// Consumes the channel and writes an epitaph.
    fn close_with_epitaph(self, status: zx::Status) -> Result<(), Error>;
}

impl ChannelEpitaphExt for zx::Channel {
    fn close_with_epitaph(self, status: zx::Status) -> Result<(), Error> {
        write_epitaph_impl(&self, status)
    }
}

impl ChannelEpitaphExt for fasync::Channel {
    fn close_with_epitaph(self, status: zx::Status) -> Result<(), Error> {
        write_epitaph_impl(&self, status)
    }
}

pub(crate) trait ChannelLike {
    fn write(&self, bytes: &[u8], handles: &mut Vec<zx::Handle>) -> Result<(), zx::Status>;
}

impl ChannelLike for zx::Channel {
    fn write(&self, bytes: &[u8], handles: &mut Vec<zx::Handle>) -> Result<(), zx::Status> {
        self.write(bytes, handles)
    }
}

impl ChannelLike for fasync::Channel {
    fn write(&self, bytes: &[u8], handles: &mut Vec<zx::Handle>) -> Result<(), zx::Status> {
        self.write(bytes, handles)
    }
}

pub(crate) fn write_epitaph_impl<T: ChannelLike>(
    channel: &T,
    status: zx::Status,
) -> Result<(), Error> {
    let mut msg = TransactionMessage {
        header: TransactionHeader { tx_id: 0, ordinal: encoding::EPITAPH_ORDINAL },
        body: &mut EpitaphBody { error: status },
    };
    encoding::with_tls_encoded(&mut msg, |bytes, handles| {
        channel.write(&*bytes, &mut *handles).map_err(Error::ServerEpitaphWrite)
    })
}
