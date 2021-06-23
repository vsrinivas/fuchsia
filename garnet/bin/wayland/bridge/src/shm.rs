// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fidl_fuchsia_images as images;
use fuchsia_trace as ftrace;
use fuchsia_wayland_core as wl;
use std::rc::Rc;
use wayland::*;

use crate::buffer::{Buffer, Memory};
use crate::client::Client;
use crate::object::{NewObjectExt, ObjectRef, RequestReceiver};

/// The set of pixel formats that will be announced to clients.
const SUPPORTED_PIXEL_FORMATS: &[wl_shm::Format] =
    &[wl_shm::Format::Argb8888, wl_shm::Format::Xrgb8888];

/// Converts a wayland pixel format to the Fuchsia counterpart.
///
/// Returns `None` if the format is not supported.
fn pixel_format_wl_to_fuchsia(wl_format: wl_shm::Format) -> Option<images::PixelFormat> {
    match wl_format {
        wl_shm::Format::Argb8888 | wl_shm::Format::Xrgb8888 => Some(images::PixelFormat::Bgra8),
        _ => None,
    }
}

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
        request: WlShmRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        let WlShmRequest::CreatePool { id, fd, size } = request;
        let memory = Memory::new(fd.into(), size as u64, images::MemoryType::HostMemory);
        id.implement(client, ShmPool { memory: Rc::new(memory) })?;
        Ok(())
    }
}

pub struct ShmPool {
    memory: Rc<Memory>,
}

impl ShmPool {
    /// Creates a new wl_buffer object backed by memory from this wl_shm_pool.
    ///
    /// It's possible for the resultant wl_buffer to outlive this pool, so the
    /// buffer must hold a strong reference to any resources it depends on
    /// post-creation.
    pub fn create_buffer(
        this: ObjectRef<Self>,
        client: &mut Client,
        offset: u32,
        width: i32,
        height: i32,
        stride: i32,
        format: wl_shm::Format,
    ) -> Result<Buffer, Error> {
        ftrace::duration!("wayland", "ShmPool::create_buffer");
        // TODO(tjdetwiler): Support sending the protocol-modeled errors,
        // ex: wl_shm::Error::InvalidFormat.
        let format = pixel_format_wl_to_fuchsia(format)
            .ok_or_else(|| format_err!("Invalid pixel format {:?}", format))?;

        let image_info = images::ImageInfo {
            width: width as u32,
            height: height as u32,
            stride: stride as u32,
            pixel_format: format,
            transform: images::Transform::Normal,
            color_space: images::ColorSpace::Srgb,
            tiling: images::Tiling::Linear,
            alpha_format: images::AlphaFormat::Opaque,
        };
        let this = this.get(client)?;
        Ok(Buffer::new(this.memory.clone(), offset, image_info))
    }
}

impl RequestReceiver<WlShmPool> for ShmPool {
    fn receive(
        this: ObjectRef<Self>,
        request: WlShmPoolRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlShmPoolRequest::Destroy => {
                client.delete_id(this.id())?;
            }
            WlShmPoolRequest::CreateBuffer { id, offset, width, height, stride, format } => {
                if offset < 0 {
                    return Err(format_err!("Negative buffer offset not supported: {}", offset));
                }
                let offset = offset as u32;

                let buffer = Self::create_buffer(
                    this,
                    client,
                    offset,
                    width,
                    height,
                    stride,
                    format.as_enum().unwrap(),
                )?;
                id.implement(client, buffer)?;
            }
            WlShmPoolRequest::Resize { .. } => {}
        }
        Ok(())
    }
}
