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
    zwp_linux_dmabuf_v1_server_protocol::{
        ZwpLinuxBufferParamsV1, ZwpLinuxBufferParamsV1Request, ZwpLinuxDmabufV1,
        ZwpLinuxDmabufV1Event, ZwpLinuxDmabufV1Request,
    },
};

const DRM_FORMAT_ARGB8888: u32 = 0x34325241;
const DRM_FORMAT_ABGR8888: u32 = 0x34324241;
const DRM_FORMAT_XRGB8888: u32 = 0x34325258;
const DRM_FORMAT_XBGR8888: u32 = 0x34324258;

const DRM_FORMAT_MOD_LINEAR: u64 = 0;

/// The set of pixel formats that will be announced to clients.
const SUPPORTED_PIXEL_FORMATS: &[(u32, bool)] = &[
    (DRM_FORMAT_ARGB8888, true),
    (DRM_FORMAT_ABGR8888, true),
    (DRM_FORMAT_XRGB8888, false),
    (DRM_FORMAT_XBGR8888, false),
];

/// The set of modifiers that will be announced to clients.
#[cfg(feature = "i915")]
const SUPPORTED_MODIFIERS: &[u64] = &[
    DRM_FORMAT_MOD_LINEAR,
    1 << 56 | 1, // I915_FORMAT_MOD_X_TILED
    1 << 56 | 2, // I915_FORMAT_MOD_Y_TILED
];
#[cfg(not(feature = "i915"))]
const SUPPORTED_MODIFIERS: &[u64] = &[DRM_FORMAT_MOD_LINEAR];

/// The zwp_linux_dmabuf_v1 global.
pub struct LinuxDmabuf {
    client_version: u32,
}

impl LinuxDmabuf {
    /// Creates a new `LinuxDmabuf`.
    pub fn new(client_version: u32) -> Self {
        Self { client_version }
    }

    /// Posts events back to the client for each supported pixel format.
    pub fn post_formats(&self, this: wl::ObjectId, client: &Client) -> Result<(), Error> {
        for (format, _) in SUPPORTED_PIXEL_FORMATS.iter() {
            client.event_queue().post(this, ZwpLinuxDmabufV1Event::Format { format: *format })?;
            if self.client_version >= 3 {
                for modifier in SUPPORTED_MODIFIERS.iter() {
                    client.event_queue().post(
                        this,
                        ZwpLinuxDmabufV1Event::Modifier {
                            format: *format,
                            modifier_hi: (modifier >> 32) as u32,
                            modifier_lo: (modifier & 0xffffffff) as u32,
                        },
                    )?;
                }
            }
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
    pub fn create(&mut self, width: i32, height: i32, format: u32) -> Result<Buffer, Error> {
        let image_size = Size { width, height };
        let has_alpha = SUPPORTED_PIXEL_FORMATS
            .iter()
            .find_map(
                |&(supported_format, has_alpha)| {
                    if supported_format == format {
                        Some(has_alpha)
                    } else {
                        None
                    }
                },
            )
            .unwrap_or(false);
        let raw_import_token = self.token.duplicate_handle(zx::Rights::SAME_RIGHTS)?;
        let import_token = composition::BufferCollectionImportToken { value: raw_import_token };

        Ok(Buffer::from_import_token(Rc::new(import_token), image_size, has_alpha))
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
            ZwpLinuxBufferParamsV1Request::CreateImmed {
                buffer_id, width, height, format, ..
            } => {
                let buffer = this.get_mut(client)?.create(width, height, format)?;
                buffer_id.implement(client, buffer)?;
            }
        }
        Ok(())
    }
}
