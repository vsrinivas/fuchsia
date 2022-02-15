// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{create_endpoints, create_proxy, ClientEnd, Proxy},
    fidl_fuchsia_hardware_display as fdisplay,
    fidl_fuchsia_sysmem::{
        self as fsysmem, AllocatorMarker, BufferCollectionInfo2, BufferCollectionMarker,
        BufferCollectionProxy, BufferCollectionTokenMarker, BufferCollectionTokenProxy,
        ColorSpaceType,
    },
    fuchsia_component::client::connect_to_protocol,
    fuchsia_image_format::{
        BUFFER_COLLECTION_CONSTRAINTS_DEFAULT, BUFFER_MEMORY_CONSTRAINTS_DEFAULT,
        BUFFER_USAGE_DEFAULT, IMAGE_FORMAT_CONSTRAINTS_DEFAULT,
    },
    fuchsia_zircon::{self as zx, AsHandleRef, HandleBased},
};

use crate::{
    controller::Controller,
    error::{Error, Result},
    pixel_format::PixelFormat,
    types::{CollectionId, ImageId},
};

/// Input parameters for constructing an image.
#[derive(Clone)]
pub struct ImageParameters {
    /// The width dimension of the image, in pixels.
    pub width: u32,

    /// The height dimension of the image, in pixels.
    pub height: u32,

    /// Describes how individual pixels of the image will be interpreted. Determines the pixel
    /// stride for the image buffer.
    pub pixel_format: PixelFormat,

    /// The sysmem color space standard representation. The user must take care that `color_space`
    /// is compatible with the supplied `pixel_format`.
    pub color_space: ColorSpaceType,

    /// Optional name to assign to the VMO that backs this image.
    pub name: Option<String>,
}

/// Represents an allocated image buffer that can be assigned to a display layer.
pub struct Image {
    /// The ID of the image as provided by the display driver.
    pub id: ImageId,

    /// The ID of the sysmem buffer collection that backs this image.
    pub collection_id: CollectionId,

    /// The VMO that contains the shared image buffer.
    pub vmo: zx::Vmo,

    /// The parameters that the image was initialized with.
    pub parameters: ImageParameters,

    /// The image format constraints that resulted from the sysmem buffer negotiation. Contains the
    /// effective image parameters.
    pub format_constraints: fsysmem::ImageFormatConstraints,

    /// The effective buffer memory settings that resulted from the sysmem buffer negotiation.
    pub buffer_settings: fsysmem::BufferMemorySettings,

    // The BufferCollection that backs this image.
    proxy: BufferCollectionProxy,

    // The display driver proxy that this image has been imported into.
    ctrl: Controller,
}

impl Image {
    /// Construct a new sysmem-buffer-backed image and register it with the display driver. If
    /// successful, the image can be assigned to a primary layer in a display configuration.
    pub async fn create(ctrl: Controller, params: &ImageParameters) -> Result<Image> {
        let mut collection = allocate_image_buffer(ctrl.clone(), params).await?;
        let id = ctrl.import_image(collection.id, params.into()).await?;
        let vmo = collection.info.buffers[0]
            .vmo
            .as_ref()
            .ok_or(Error::BuffersNotAllocated)?
            .duplicate_handle(zx::Rights::SAME_RIGHTS)?;

        collection.release();
        Ok(Image {
            id,
            collection_id: collection.id,
            vmo,
            parameters: params.clone(),
            format_constraints: collection.info.settings.image_format_constraints,
            buffer_settings: collection.info.settings.buffer_settings,
            proxy: collection.proxy.clone(),
            ctrl,
        })
    }
}

impl Drop for Image {
    fn drop(&mut self) {
        let _ = self.proxy.close();
        let _ = self.ctrl.release_buffer_collection(self.collection_id);
    }
}

impl From<&ImageParameters> for fdisplay::ImageConfig {
    fn from(src: &ImageParameters) -> Self {
        Self {
            width: src.width,
            height: src.height,
            pixel_format: src.pixel_format.into(),
            type_: fdisplay::TYPE_SIMPLE,
        }
    }
}

impl From<ImageParameters> for fdisplay::ImageConfig {
    fn from(src: ImageParameters) -> Self {
        fdisplay::ImageConfig::from(&src)
    }
}

// Result of `allocate_image_buffer` that automatically releases the display driver's connection to
// the buffer collection unless `release()` is called on it. This is intended to clean up resources
// in the early-return cases above.
struct BufferCollection {
    id: CollectionId,
    info: BufferCollectionInfo2,
    proxy: BufferCollectionProxy,
    ctrl: Controller,
    released: bool,
}

impl BufferCollection {
    fn release(&mut self) {
        self.released = true;
    }
}

impl Drop for BufferCollection {
    fn drop(&mut self) {
        if !self.released {
            let _ = self.ctrl.release_buffer_collection(self.id);
            let _ = self.proxy.close();
        }
    }
}

// Allocate a sysmem buffer collection and register it with the display driver. The allocated
// buffer can be used to construct a display layer image.
async fn allocate_image_buffer(
    ctrl: Controller,
    params: &ImageParameters,
) -> Result<BufferCollection> {
    let allocator =
        connect_to_protocol::<AllocatorMarker>().map_err(|_| Error::SysmemConnection)?;
    {
        let name = fuchsia_runtime::process_self().get_name()?;
        let koid = fuchsia_runtime::process_self().get_koid()?;
        allocator.set_debug_client_info(name.to_str()?, koid.raw_koid())?;
    }
    let collection_token = {
        let (proxy, remote) = create_proxy::<BufferCollectionTokenMarker>()?;
        allocator.allocate_shared_collection(remote)?;
        proxy
    };
    // TODO(armansito): The priority number here is arbitrary but I don't expect there to be
    // contention for the assigned name as this client library should be the collection's sole
    // owner. Still, come up with a better way to assign this.
    if let Some(ref name) = params.name {
        collection_token.set_name(100, name.as_str())?;
    }

    // Duplicate of `collection_token` to be transferred to the display driver.
    let display_duplicate = {
        let (local, remote) = create_endpoints::<BufferCollectionTokenMarker>()?;
        collection_token.duplicate(std::u32::MAX, remote)?;
        collection_token.sync().await?;
        local
    };

    // Register the collection with the display driver.
    let id = ctrl.import_buffer_collection(display_duplicate).await?;

    // Tell sysmem to perform the buffer allocation and wait for the result. Clean up on error.
    match allocate_image_buffer_helper(params, allocator, collection_token).await {
        Ok((info, proxy)) => Ok(BufferCollection { id, info, proxy, ctrl, released: false }),
        Err(error) => {
            let _ = ctrl.release_buffer_collection(id);
            Err(error)
        }
    }
}

async fn allocate_image_buffer_helper(
    params: &ImageParameters,
    allocator: fsysmem::AllocatorProxy,
    token: BufferCollectionTokenProxy,
) -> Result<(BufferCollectionInfo2, BufferCollectionProxy)> {
    // Turn in the collection token to obtain a connection to the logical buffer collection.
    let collection = {
        let (local, remote) = create_endpoints::<BufferCollectionMarker>()?;
        let token_channel =
            token.into_channel().map_err(|_| Error::SysmemConnection)?.into_zx_channel();
        allocator.bind_shared_collection(ClientEnd::new(token_channel), remote)?;
        local.into_proxy()?
    };

    // Set local constraints and allocate buffers.
    collection.set_constraints(true, &mut buffer_collection_constraints(params))?;
    let collection_info = {
        let (status, info) = collection.wait_for_buffers_allocated().await?;
        let _ = zx::Status::ok(status)?;
        info
    };

    // We expect there to be at least one available vmo.
    if collection_info.buffer_count == 0 {
        collection.close()?;
        return Err(Error::BuffersNotAllocated);
    }

    Ok((collection_info, collection))
}

fn buffer_collection_constraints(params: &ImageParameters) -> fsysmem::BufferCollectionConstraints {
    let usage = fsysmem::BufferUsage {
        cpu: fsysmem::CPU_USAGE_READ_OFTEN | fsysmem::CPU_USAGE_WRITE_OFTEN,
        ..BUFFER_USAGE_DEFAULT
    };

    let buffer_memory_constraints = fsysmem::BufferMemoryConstraints {
        ram_domain_supported: true,
        cpu_domain_supported: true,
        ..BUFFER_MEMORY_CONSTRAINTS_DEFAULT
    };

    // TODO(armansito): parameterize the format modifier
    let pixel_format = fsysmem::PixelFormat {
        type_: params.pixel_format.into(),
        has_format_modifier: true,
        format_modifier: fsysmem::FormatModifier { value: fsysmem::FORMAT_MODIFIER_LINEAR },
    };

    let mut image_constraints = fsysmem::ImageFormatConstraints {
        required_max_coded_width: params.width,
        required_max_coded_height: params.height,
        color_spaces_count: 1,
        pixel_format,
        ..IMAGE_FORMAT_CONSTRAINTS_DEFAULT
    };
    image_constraints.color_space[0].type_ = params.color_space;

    let mut constraints = fsysmem::BufferCollectionConstraints {
        min_buffer_count: 1,
        usage,
        has_buffer_memory_constraints: true,
        buffer_memory_constraints,
        image_format_constraints_count: 1,
        ..BUFFER_COLLECTION_CONSTRAINTS_DEFAULT
    };
    constraints.image_format_constraints[0] = image_constraints;

    constraints
}
