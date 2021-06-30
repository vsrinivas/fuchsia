// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_images as images;
use fidl_fuchsia_math::Size;
use fidl_fuchsia_ui_composition as composition;
use fuchsia_scenic as scenic;
use fuchsia_trace as ftrace;
use fuchsia_zircon::{self as zx, HandleBased};
use std::cell::Cell;
use std::rc::Rc;
use std::sync::Arc;
use wayland::*;

use crate::client::Client;
use crate::object::{ObjectRef, RequestReceiver};

pub struct Memory {
    /// The backing VMO.
    vmo: zx::Vmo,
    /// The size of `vmo`, in bytes.
    size: u64,
    /// The type of memory in `vmo`.
    memory_type: images::MemoryType,

    /// The scenic 'Memory' resource associated with this buffer.
    ///
    /// We need to create this resource in the session associated with the
    /// surface that the buffers created from this buffer will be attached to.
    ///
    /// Note we currently assume that this object will only ever be associated
    /// with a single wl_surface (and therefore only a single scenic Session)
    /// and the current implementation will panic if a wl_buffer is attached to
    /// more than a single wl_surface over the course of it's lifetime.
    ///
    /// TODO(fxb/78911): We'll want to lift this limitation as the protocol
    /// allows for the free movement of wl_buffer objects across surfaces.
    scenic_resource: Cell<Option<(Rc<scenic::Memory>, scenic::SessionPtr)>>,
}

impl Memory {
    pub fn new(vmo: zx::Vmo, size: u64, memory_type: images::MemoryType) -> Self {
        Self { vmo, size, memory_type, scenic_resource: Cell::new(None) }
    }

    pub fn scenic_resource(&self, session: &scenic::SessionPtr) -> Rc<scenic::Memory> {
        ftrace::duration!("wayland", "Memory::scenic_resource");
        let resource = match self.scenic_resource.take() {
            Some(resource) => {
                // We already have a memory resource. Verify the session that
                // owns this resource matches our current session.
                assert!(
                    Arc::ptr_eq(session, &resource.1),
                    "Migrating memory resources across sessions is not implemented"
                );
                resource
            }
            None => {
                let memory = scenic::Memory::new(
                    session.clone(),
                    self.vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap(),
                    self.size,
                    self.memory_type,
                );
                (Rc::new(memory), session.clone())
            }
        };
        let result = resource.0.clone();
        self.scenic_resource.set(Some(resource));
        result
    }
}

struct Image {
    /// The scenic import token.
    import_token: Rc<composition::BufferCollectionImportToken>,
    /// The size of the image.
    size: Size,
    /// The scenic 'Image' resource associated with this buffer.
    ///
    /// We need to create this resource in the session associated with the
    /// surface that the buffers created from this buffer will be attached to.
    ///
    /// Note we currently assume that this object will only ever be associated
    /// with a single wl_surface (and therefore only a single scenic Session)
    /// and the current implementation will panic if a wl_buffer is attached to
    /// more than a single wl_surface over the course of it's lifetime.
    ///
    /// TODO(fxb/78911): We'll want to lift this limitation as the protocol
    /// allows for the free movement of wl_buffer objects across surfaces.
    resource: Cell<Option<(Rc<scenic::Image3>, scenic::SessionPtr)>>,
}

impl Image {
    pub fn new(import_token: Rc<composition::BufferCollectionImportToken>, size: Size) -> Self {
        Self { import_token, size, resource: Cell::new(None) }
    }

    pub fn scenic_resource(&self, session: &scenic::SessionPtr) -> Rc<scenic::Image3> {
        ftrace::duration!("wayland", "Image::scenic_resource");
        let resource = match self.resource.take() {
            Some(resource) => {
                // We already have a image resource. Verify the session that
                // owns this resource matches our current session.
                assert!(
                    Arc::ptr_eq(session, &resource.1),
                    "Migrating image resources across sessions is not implemented"
                );
                resource
            }
            None => {
                let raw_import_token =
                    self.import_token.value.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();
                let image = scenic::Image3::new(
                    session,
                    self.size.width as u32,
                    self.size.height as u32,
                    composition::BufferCollectionImportToken { value: raw_import_token },
                    0,
                );
                (Rc::new(image), session.clone())
            }
        };
        let result = resource.0.clone();
        self.resource.set(Some(resource));
        result
    }
}

/// An implementation of 'wl_buffer'.
pub struct Buffer {
    // The scenic `Memory` object that will back this buffer.
    memory: Option<Rc<Memory>>,
    /// The offset into `memory` that this image is located.
    offset: u32,
    // The scenic `Image` object that will back this buffer.
    image: Option<Rc<Image>>,
    /// Additional description of the format of the buffer needed to draw the
    /// image correctly.
    image_info: images::ImageInfo,
}

/// Allow this to be `Clone` so that surfaces can keep the underlying
/// `scenic::Memory` or `scenic::Image3` alive even if the buffer
/// protocol object has been released.
impl Clone for Buffer {
    fn clone(&self) -> Self {
        Buffer {
            memory: self.memory.clone(),
            offset: self.offset,
            image: self.image.clone(),
            image_info: images::ImageInfo { ..self.image_info },
        }
    }
}

impl Buffer {
    pub fn new(memory: Rc<Memory>, offset: u32, image_info: images::ImageInfo) -> Self {
        Buffer { memory: Some(memory), offset, image: None, image_info }
    }

    pub fn from_import_token(
        import_token: Rc<composition::BufferCollectionImportToken>,
        image_size: Size,
    ) -> Self {
        let image = Image::new(import_token, image_size);
        let image_info = images::ImageInfo {
            width: image_size.width as u32,
            height: image_size.height as u32,
            stride: 0,
            pixel_format: images::PixelFormat::Bgra8,
            transform: images::Transform::Normal,
            color_space: images::ColorSpace::Srgb,
            tiling: images::Tiling::Linear,
            alpha_format: images::AlphaFormat::Opaque,
        };
        Buffer { memory: None, offset: 0, image: Some(Rc::new(image)), image_info }
    }

    pub fn image_size(&self) -> Size {
        Size { width: self.image_info.width as i32, height: self.image_info.height as i32 }
    }

    pub fn create_image(&self, session: &scenic::SessionPtr) -> Option<scenic::Image> {
        ftrace::duration!("wayland", "Buffer::create_image");
        self.memory.as_ref().and_then(|memory| {
            let image_info = images::ImageInfo { ..self.image_info };
            let scenic_resource = memory.scenic_resource(session);
            Some(scenic::Image::new(&*scenic_resource, self.offset, image_info))
        })
    }

    pub fn image3_resource(&self, session: &scenic::SessionPtr) -> Option<Rc<scenic::Image3>> {
        ftrace::duration!("wayland", "Buffer::image3_resource");
        self.image.as_ref().and_then(|image| Some(image.scenic_resource(session)))
    }
}

impl RequestReceiver<WlBuffer> for Buffer {
    fn receive(
        this: ObjectRef<Self>,
        request: WlBufferRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        let WlBufferRequest::Destroy = request;
        client.delete_id(this.id())?;
        Ok(())
    }
}
