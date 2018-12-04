// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};
use fidl_fuchsia_images as images;
use fuchsia_scenic as scenic;
use fuchsia_wayland_core as wl;
use std::rc::Rc;
use wayland::*;

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
pub struct Shm {
    scenic: scenic::SessionPtr,
}

impl Shm {
    /// Creates a new `Shm`.
    pub fn new(scenic: scenic::SessionPtr) -> Self {
        Shm { scenic }
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
        this: wl::ObjectRef<Self>, request: WlShmRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        let WlShmRequest::CreatePool { id, fd, size } = request;
        let scenic = this.get(client)?.scenic.clone();
        let memory = scenic::Memory::new(scenic.clone(), fd.into(), size as u64, images::MemoryType::HostMemory);
        id.implement(
            client,
            ShmPool {
                memory: Rc::new(memory),
                _size: size,
            },
        )?;
        Ok(())
    }
}

pub struct ShmPool {
    /// The scenic memory object that wraps the VMO handle sent from the
    /// client.
    memory: Rc<scenic::Memory>,

    /// The size of `memory`, in bytes.
    _size: i32,
}

impl ShmPool {
    /// Creates a new wl_buffer object backed by memory from this wl_shm_pool.
    ///
    /// It's possible for the resultant wl_buffer to outlive this pool, so the
    /// buffer must hold a strong reference to any resources it depends on
    /// post-creation.
    pub fn create_buffer(
        this: wl::ObjectRef<Self>, client: &mut wl::Client, offset: u32, width: i32, height: i32,
        stride: i32, format: wl_shm::Format,
    ) -> Result<Buffer, Error> {
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
        Ok(Buffer {
            memory: this.memory.clone(),
            offset,
            image_info,
        })
    }
}

impl wl::RequestReceiver<WlShmPool> for ShmPool {
    fn receive(
        this: wl::ObjectRef<Self>, request: WlShmPoolRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        match request {
            WlShmPoolRequest::Destroy => {
                client.delete_id(this.id())?;
            }
            WlShmPoolRequest::CreateBuffer {
                id,
                offset,
                width,
                height,
                stride,
                format,
            } => {
                if offset < 0 {
                    return Err(format_err!(
                        "Negative buffer offset not supported: {}",
                        offset
                    ));
                }
                let offset = offset as u32;

                let buffer = Self::create_buffer(
                    this,
                    client,
                    offset,
                    width,
                    height,
                    stride,
                    format.as_enum()?,
                )?;
                id.implement(client, buffer)?;
            }
            WlShmPoolRequest::Resize { .. } => {}
        }
        Ok(())
    }
}

/// An implementation of 'wl_buffer'.
pub struct Buffer {
    /// The scenic `Memory` object that will back this buffer.
    memory: Rc<scenic::Memory>,
    /// The offest into `memory` that this image is located.
    offset: u32,
    /// Additional description of the format of the buffer needed to draw the
    /// image correctly.
    image_info: images::ImageInfo,
}

/// Allow this to be `Clone` so that surfaces can keep the underlying
/// `scenic::Memory` alive even if the buffer protocol object has been released.
impl Clone for Buffer {
    fn clone(&self) -> Self {
        Buffer {
            memory: self.memory.clone(),
            offset: self.offset,
            image_info: images::ImageInfo { ..self.image_info },
        }
    }
}

impl Buffer {
    pub fn image_info(&self) -> &images::ImageInfo {
        &self.image_info
    }

    /// Create a new scenic `Image` for this buffer. This entity must be
    /// recreated after each change to the backing `Memory` in order for scenic
    /// to present any changes to the `Memory` since it was last presented.
    pub fn create_image(&self) -> scenic::Image {
        let image_info = images::ImageInfo { ..self.image_info };
        scenic::Image::new(&*self.memory, self.offset, image_info)
    }
}

impl wl::RequestReceiver<WlBuffer> for Buffer {
    fn receive(
        this: wl::ObjectRef<Self>, request: WlBufferRequest, client: &mut wl::Client,
    ) -> Result<(), Error> {
        let WlBufferRequest::Destroy = request;
        client.delete_id(this.id())?;
        Ok(())
    }
}
