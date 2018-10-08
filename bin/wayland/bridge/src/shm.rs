// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fuchsia_wayland_core as wl;
use wayland::*;

/// The set of pixel formats that will be announced to clients.
const SUPPORTED_PIXEL_FORMATS: &[wl_shm::Format] = &[
    wl_shm::Format::Argb8888,
    wl_shm::Format::Xrgb8888,
];

pub struct Shm;

impl Shm {
    pub fn new() -> Self {
        Shm
    }

    /// Posts an event back to the client for each supported SHM pixel format.
    pub fn post_formats(&self, this: wl::ObjectId, client: &wl::Client) -> Result<(), Error> {
        for format in SUPPORTED_PIXEL_FORMATS.iter() {
            client.post(this, WlShmEvent::Format { format: *format })?;
        }
        Ok(())
    }
}

impl wl::RequestReceiver<WlShm> for Shm {
    fn receive(
        _this: wl::ObjectRef<Self>, request: WlShmRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        let WlShmRequest::CreatePool { id, size, .. } = request;
        println!("wl_shm::create_pool(id: {}, fd, size: {})", id, size);
        client.objects().add_object(WlShmPool, id, ShmPool::new())?;
        Ok(())
    }
}

pub struct ShmPool;

impl ShmPool {
    pub fn new() -> Self {
        ShmPool
    }
}

impl wl::RequestReceiver<WlShmPool> for ShmPool {
    fn receive(
        _this: wl::ObjectRef<Self>, request: WlShmPoolRequest, _client: &mut wl::Client,
    ) -> Result<(), Error> {
        match request {
            WlShmPoolRequest::Destroy {} => {
                println!("wl_shm_pool::destroy");
            }
            WlShmPoolRequest::CreateBuffer {
                id,
                offset,
                width,
                height,
                stride,
                format,
            } => {
                println!(
                    "wl_shm_pool::create_buffer(id: {}, offset: {}, width: {}, height: {}, \
                     stride: {}, format: {:?})",
                    id, offset, width, height, stride, format
                );
            }
            WlShmPoolRequest::Resize { size } => {
                println!("wl_shm_pool::resize(size: {})", size);
            }
        }
        Ok(())
    }
}
