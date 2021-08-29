// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::object::{ObjectRef, RequestReceiver},
    anyhow::Error,
    fidl_fuchsia_math::Size,
    fidl_fuchsia_ui_composition as composition, fuchsia_trace as ftrace,
    fuchsia_zircon::{self as zx, HandleBased},
    std::{cell::Cell, rc::Rc},
    wayland::*,
};

#[cfg(feature = "flatland")]
use {
    crate::scenic::{Flatland, FlatlandInstanceId},
    fidl_fuchsia_math as fmath,
    fidl_fuchsia_ui_composition::{BufferCollectionImportToken, ContentId, ImageProperties},
};

#[cfg(not(feature = "flatland"))]
use {fuchsia_scenic as scenic, std::sync::Arc};

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
    /// Note we currently assume that this object will only ever be associated
    /// with a single wl_surface (and therefore only a single Flatland instance)
    /// and the current implementation will panic if a wl_buffer is attached to
    /// more than a single wl_surface over the course of it's lifetime.
    ///
    /// TODO(fxb/78911): We'll want to lift this limitation as the protocol
    /// allows for the free movement of wl_buffer objects across surfaces.
    #[cfg(feature = "flatland")]
    id: Cell<Option<(ContentId, FlatlandInstanceId)>>,
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
    #[cfg(not(feature = "flatland"))]
    resource: Cell<Option<(Rc<scenic::Image3>, scenic::SessionPtr)>>,
}

impl Image {
    fn size(&self) -> Size {
        self.size
    }
}

#[cfg(feature = "flatland")]
impl Image {
    pub fn new(import_token: Rc<composition::BufferCollectionImportToken>, size: Size) -> Self {
        Self { import_token, size, id: Cell::new(None) }
    }

    pub fn scenic_id(&self, flatland: &Flatland) -> ContentId {
        ftrace::duration!("wayland", "Image::scenic_id");
        let id = match self.id.take() {
            Some(id) => {
                // We already have an image ID. Verify that the Flatland instance
                // that owns this ID matches our current instance.
                assert!(
                    flatland.id() == id.1,
                    "Migrating image IDs across Flatland instances is not implemented"
                );
                id
            }
            None => {
                let raw_import_token =
                    self.import_token.value.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();
                let mut size =
                    fmath::SizeU { width: self.size.width as u32, height: self.size.height as u32 };
                let image_props = ImageProperties { size: Some(size), ..ImageProperties::EMPTY };
                let content_id = flatland.alloc_content_id();
                let mut import_token = BufferCollectionImportToken { value: raw_import_token };
                flatland
                    .proxy()
                    .create_image(&mut content_id.clone(), &mut import_token, 0, image_props)
                    .expect("fidl error");
                flatland
                    .proxy()
                    .set_image_destination_size(&mut content_id.clone(), &mut size)
                    .expect("fidl error");

                (content_id, flatland.id())
            }
        };
        let result = id.0.clone();
        self.id.set(Some(id));
        result
    }
}

#[cfg(not(feature = "flatland"))]
impl Image {
    pub fn new(import_token: Rc<composition::BufferCollectionImportToken>, size: Size) -> Self {
        Self { import_token, size, resource: Cell::new(None) }
    }

    pub fn scenic_resource(&self, session: &scenic::SessionPtr) -> Rc<scenic::Image3> {
        ftrace::duration!("wayland", "Image::scenic_resource");
        let resource = match self.resource.take() {
            Some(resource) => {
                // We already have an image resource. Verify the session that
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
#[derive(Clone)]
pub struct Buffer {
    // The scenic `Image` object that will back this buffer.
    image: Rc<Image>,
}

impl Buffer {
    pub fn from_import_token(
        import_token: Rc<composition::BufferCollectionImportToken>,
        image_size: Size,
    ) -> Self {
        let image = Image::new(import_token, image_size);
        Buffer { image: Rc::new(image) }
    }

    pub fn image_size(&self) -> Size {
        self.image.size()
    }
}

#[cfg(feature = "flatland")]
impl Buffer {
    pub fn image_id(&self, flatland: &Flatland) -> ContentId {
        ftrace::duration!("wayland", "Buffer::image_id");
        self.image.scenic_id(flatland)
    }
}

#[cfg(not(feature = "flatland"))]
impl Buffer {
    pub fn image_resource(&self, session: &scenic::SessionPtr) -> Rc<scenic::Image3> {
        ftrace::duration!("wayland", "Buffer::image_resource");
        self.image.scenic_resource(session)
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
