// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::buffer::Buffer,
    crate::client::Client,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    anyhow::Error,
    fidl_fuchsia_math::Size,
    fidl_fuchsia_ui_composition as composition, fuchsia_wayland_core as wl, fuchsia_zircon as zx,
    fuchsia_zircon::{EventPair, Handle, HandleBased},
    std::rc::Rc,
    zwp_linux_dmabuf_v1::{
        ZwpLinuxBufferParamsV1, ZwpLinuxBufferParamsV1Request, ZwpLinuxDmabufV1,
        ZwpLinuxDmabufV1Event, ZwpLinuxDmabufV1Request,
    },
};

const DRM_FORMAT_ARGB8888: u32 = 0x34325241;
const DRM_FORMAT_ABGR8888: u32 = 0x34324241;
const DRM_FORMAT_XRGB8888: u32 = 0x34325258;
const DRM_FORMAT_XBGR8888: u32 = 0x34324258;

/// The set of pixel formats that will be announced to clients.
const SUPPORTED_PIXEL_FORMATS: &[u32] =
    &[DRM_FORMAT_ARGB8888, DRM_FORMAT_ABGR8888, DRM_FORMAT_XRGB8888, DRM_FORMAT_XBGR8888];

/// The zwp_linux_dmabuf_v1 global.
pub struct LinuxDmabuf;

impl LinuxDmabuf {
    /// Creates a new `LinuxDmabuf`.
    pub fn new() -> Self {
        Self
    }

    /// Posts an event back to the client for each supported pixel format.
    pub fn post_formats(&self, this: wl::ObjectId, client: &Client) -> Result<(), Error> {
        for format in SUPPORTED_PIXEL_FORMATS.iter() {
            client.event_queue().post(this, ZwpLinuxDmabufV1Event::Format { format: *format })?;
        }
        Ok(())
    }
}

impl RequestReceiver<ZwpLinuxDmabufV1> for LinuxDmabuf {
    fn receive(
        this: ObjectRef<Self>,
        request: ZwpLinuxDmabufV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZwpLinuxDmabufV1Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZwpLinuxDmabufV1Request::CreateParams { params_id } => {
                params_id.implement(client, LinuxBufferParams::new())?;
            }
        }
        Ok(())
    }
}

pub struct LinuxBufferParams {
    /// The token sent from the client.
    token: EventPair,
}

impl LinuxBufferParams {
    /// Creates a new `LinuxDmabufParams`.
    pub fn new() -> Self {
        LinuxBufferParams { token: Handle::invalid().into() }
    }

    /// Creates a new wl_buffer object backed by memory from params.
    pub fn create(&mut self, width: i32, height: i32) -> Result<Buffer, Error> {
        let image_size = Size { width, height };
        let raw_import_token = self.token.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
        let import_token = composition::BufferCollectionImportToken { value: raw_import_token };

        Ok(Buffer::from_import_token(Rc::new(import_token), image_size))
    }

    pub fn set_plane(&mut self, token: EventPair) {
        self.token = token;
    }
}

impl RequestReceiver<ZwpLinuxBufferParamsV1> for LinuxBufferParams {
    fn receive(
        this: ObjectRef<Self>,
        request: ZwpLinuxBufferParamsV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZwpLinuxBufferParamsV1Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZwpLinuxBufferParamsV1Request::Add { fd, .. } => {
                this.get_mut(client)?.set_plane(fd.into());
            }
            ZwpLinuxBufferParamsV1Request::Create { .. } => {}
            ZwpLinuxBufferParamsV1Request::CreateImmed { buffer_id, width, height, .. } => {
                let buffer = this.get_mut(client)?.create(width, height)?;
                buffer_id.implement(client, buffer)?;
            }
        }
        Ok(())
    }
}
