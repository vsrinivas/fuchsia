// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fuchsia_trace as ftrace;
use fuchsia_wayland_core as wl;
use wayland::*;

use crate::client::Client;
use crate::object::{ObjectRef, RequestReceiver};

/// The set of pixel formats that will be announced to clients.
/// Note: We don't actually support any shm formats but not reporting these as
/// supported will confuse Sommelier even if they will never be used. Sommelier
/// will use dmabuf protocol as that is available.
const SUPPORTED_PIXEL_FORMATS: &[wl_shm::Format] =
    &[wl_shm::Format::Argb8888, wl_shm::Format::Xrgb8888];

/// The wl_shm global.
pub struct Shm;

impl Shm {
    /// Creates a new `Shm`.
    pub fn new() -> Self {
        Self
    }

    /// Posts an event back to the client for each supported SHM pixel format.
    pub fn post_formats(&self, this: wl::ObjectId, client: &Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "Shm::post_formats");
        for format in SUPPORTED_PIXEL_FORMATS.iter() {
            client.event_queue().post(this, WlShmEvent::Format { format: *format })?;
        }
        Ok(())
    }
}

impl RequestReceiver<WlShm> for Shm {
    fn receive(
        _this: ObjectRef<Self>,
        _request: WlShmRequest,
        _client: &mut Client,
    ) -> Result<(), Error> {
        Err(format_err!("Shm::receive not supported"))
    }
}
