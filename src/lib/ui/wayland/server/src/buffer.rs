// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::object::{ObjectRef, RequestReceiver},
    crate::scenic::{FlatlandInstanceId, FlatlandPtr},
    anyhow::Error,
    fidl_fuchsia_math as fmath,
    fidl_fuchsia_math::Size,
    fidl_fuchsia_ui_composition as composition,
    fidl_fuchsia_ui_composition::{BufferCollectionImportToken, ContentId, ImageProperties},
    fuchsia_trace as ftrace,
    fuchsia_zircon::{self as zx, HandleBased},
    std::{cell::Cell, rc::Rc},
    wayland_server_protocol::*,
};

pub type ImageInstanceId = usize;

/// Wrapper around a content ID that provides automatic release of image by
/// implementing the Drop trait and calling release_image.
pub struct Content {
    /// The Flatland content ID.
    pub id: ContentId,
    /// The Flatland instance that was used to create content ID.
    flatland: FlatlandPtr,
}

impl Drop for Content {
    fn drop(&mut self) {
        self.flatland.borrow().proxy().release_image(&mut self.id.clone()).expect("fidl error");
    }
}

struct Image {
    /// The scenic import token.
    import_token: Rc<composition::BufferCollectionImportToken>,
    /// The size of the image.
    size: Size,
    /// The scenic 'Image' ID associated with this buffer.
    ///
    /// We need to create this ID in the Flatland instance associated with the
    /// surface that the buffers created from this buffer will be attached to.
    ///
    /// Note we currently assume that this object is typically only associated
    /// with a single wl_surface (and therefore only a single Flatland instance)
    /// and the current implementation re-import the wl_buffer if it is attached
    /// to more than a single wl_surface over the course of it's lifetime.
    id: Cell<Option<(Rc<Content>, ImageInstanceId, FlatlandInstanceId)>>,
}

impl Image {
    fn size(&self) -> Size {
        self.size
    }

    pub fn new(import_token: Rc<composition::BufferCollectionImportToken>, size: Size) -> Self {
        Self { import_token, size, id: Cell::new(None) }
    }

    pub fn scenic_content(
        &self,
        instance_id: ImageInstanceId,
        flatland: &FlatlandPtr,
    ) -> Rc<Content> {
        ftrace::duration!("wayland", "Image::scenic_content");
        let flatland_id = flatland.borrow().id();
        let id = match self.id.take().filter(|id| instance_id == id.1 && flatland_id == id.2) {
            Some(id) => id,
            None => {
                let raw_import_token =
                    self.import_token.value.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();
                let size =
                    fmath::SizeU { width: self.size.width as u32, height: self.size.height as u32 };
                let image_props = ImageProperties { size: Some(size), ..ImageProperties::EMPTY };
                let content_id = flatland.borrow_mut().alloc_content_id();
                let mut import_token = BufferCollectionImportToken { value: raw_import_token };
                flatland
                    .borrow()
                    .proxy()
                    .create_image(&mut content_id.clone(), &mut import_token, 0, image_props)
                    .expect("fidl error");
                let content = Content { id: content_id, flatland: flatland.clone() };

                (Rc::new(content), instance_id, flatland_id)
            }
        };
        let result = id.0.clone();
        self.id.set(Some(id));
        result
    }
}

/// An implementation of 'wl_buffer'.
#[derive(Clone)]
pub struct Buffer {
    // The scenic `Image` object that will back this buffer.
    image: Rc<Image>,
    // Set to true if buffer contains an alpha channel.
    has_alpha: bool,
}

impl Buffer {
    pub fn from_import_token(
        import_token: Rc<composition::BufferCollectionImportToken>,
        image_size: Size,
        has_alpha: bool,
    ) -> Self {
        let image = Image::new(import_token, image_size);
        Buffer { image: Rc::new(image), has_alpha }
    }

    pub fn image_size(&self) -> Size {
        self.image.size()
    }

    pub fn has_alpha(&self) -> bool {
        self.has_alpha
    }

    pub fn image_content(
        &self,
        instance_id: ImageInstanceId,
        flatland: &FlatlandPtr,
    ) -> Rc<Content> {
        ftrace::duration!("wayland", "Buffer::image_content");
        self.image.scenic_content(instance_id, flatland)
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
