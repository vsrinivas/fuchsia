// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::scenic::types::ScreenshotDataDef;
use anyhow::{Context as _, Error};
use fidl::endpoints::{ClientEnd, Proxy};
use fidl_fuchsia_sysmem::{AllocatorMarker as SysmemAllocatorMarker, *};
use fidl_fuchsia_ui_app::{ViewConfig, ViewMarker, ViewProviderMarker};
use fidl_fuchsia_ui_composition::{
    self as ui_comp, AllocatorMarker as ScenicAllocatorMarker, ScreenshotMarker,
};
use fidl_fuchsia_ui_policy::PresenterMarker;
use fidl_fuchsia_ui_scenic::{ScenicMarker, ScreenshotData};
use fuchsia_component::{
    self as app,
    client::{launch, launcher},
};
use fuchsia_scenic::{self as scenic};
use fuchsia_zircon::{self as zx, AsHandleRef, DurationNum, HandleBased};
use serde_json::{to_value, Value};

/// Perform Scenic operations.
///
/// Note this object is shared among all threads created by server.
///
/// This facade does not hold onto a Scenic proxy as the server may be
/// long-running while individual tests set up and tear down Scenic.
#[derive(Debug)]
pub struct ScenicFacade {}

// Override these fields with other values if necessary.
const IMAGE_FORMAT_CONSTRAINTS_DEFAULT: ImageFormatConstraints = ImageFormatConstraints {
    // Probably need to change these.
    required_min_coded_width: 1280,
    required_max_coded_width: 1280,
    required_min_coded_height: 800,
    required_max_coded_height: 800,

    // Probably don't need to change these.
    pixel_format: PixelFormat {
        type_: PixelFormatType::Bgra32,
        has_format_modifier: true,
        format_modifier: FormatModifier { value: 0 },
    },
    color_spaces_count: 1,
    color_space: [ColorSpace { type_: ColorSpaceType::Srgb }; 32],
    bytes_per_row_divisor: 4,

    min_coded_width: 0,
    max_coded_width: 0,
    min_coded_height: 0,
    max_coded_height: 0,
    min_bytes_per_row: 0,
    max_bytes_per_row: 0,
    max_coded_width_times_coded_height: 0,
    layers: 0,
    coded_width_divisor: 0,
    coded_height_divisor: 0,
    start_offset_divisor: 0,
    display_width_divisor: 0,
    display_height_divisor: 0,
    required_min_bytes_per_row: 0,
    required_max_bytes_per_row: 0,
};

// Override these fields with other values if necessary.
const BUFFER_MEMORY_CONSTRAINTS_DEFAULT: BufferMemoryConstraints = BufferMemoryConstraints {
    min_size_bytes: 0,
    max_size_bytes: std::u32::MAX,
    physically_contiguous_required: false,
    secure_required: false,
    ram_domain_supported: false,
    cpu_domain_supported: true,
    inaccessible_domain_supported: false,
    heap_permitted_count: 0,
    heap_permitted: [HeapType::SystemRam; 32],
};

impl ScenicFacade {
    pub fn new() -> ScenicFacade {
        ScenicFacade {}
    }

    pub async fn take_screenshot(&self) -> Result<Value, Error> {
        let scenic = app::client::connect_to_protocol::<ScenicMarker>()
            .context("Failed to connect to Scenic")?;

        // TODO(fxbug.dev/64206): Remove after Flatland migration is completed.
        let using_flatland: bool =
            scenic.uses_flatland().await.context("Failed to get flatland info.")?;

        // If we are using Gfx, take screenshot and return.
        if !using_flatland {
            let (screenshot, success) =
                scenic.take_screenshot().await.context("Failed to take Gfx screenshot")?;

            if success {
                return Ok(to_value(ScreenshotDataDef::new(screenshot))?);
            } else {
                return Err(format_err!("Scenic.TakeScreenshot failed"));
            }
        }

        // We are using Flatland.

        let display_info = scenic.get_display_info().await.context("Failed to get display info")?;
        let width = display_info.width_in_px;
        let height = display_info.height_in_px;
        const IMAGE_ID: Option<u64> = Some(1);

        // Connect to the relevant protocols.
        let screenshotter = app::client::connect_to_protocol::<ScreenshotMarker>()
            .context("Failed to connect to screenshot")?;
        let sysmem_allocator = app::client::connect_to_protocol::<SysmemAllocatorMarker>()
            .context("Failed to connect to sysmem allocator")?;
        let scenic_allocator = app::client::connect_to_protocol::<ScenicAllocatorMarker>()
            .context("Failed to connect to scenic allocator")?;

        // First, create a sysmem BufferCollectionInfo. This will be used by us and the Screenshot
        // server to coordinate image requirements via the Allocator protocol.
        let (local_token, local_token_request) =
            fidl::endpoints::create_proxy::<BufferCollectionTokenMarker>()?;

        sysmem_allocator
            .allocate_shared_collection(local_token_request)
            .context("Failed to allocate shared collection")?;

        let (dup_token, dup_token_request) = fidl::endpoints::create_endpoints::<
            fidl_fuchsia_sysmem::BufferCollectionTokenMarker,
        >()?;
        local_token.duplicate(std::u32::MAX, dup_token_request)?;
        local_token.sync().await?;

        let local_clientend_token =
            ClientEnd::new(local_token.into_channel().unwrap().into_zx_channel());

        // Create BufferCollection{Import,Export}Token eventpair. We will pass one end to Allocator and the other to Screenshot.
        let (import_token, export_token) =
            fidl::EventPair::create().context("Failed to create event pair")?;
        let import_token = ui_comp::BufferCollectionImportToken { value: import_token };
        let export_token = ui_comp::BufferCollectionExportToken { value: export_token };

        let args = ui_comp::RegisterBufferCollectionArgs {
            export_token: Some(export_token),
            buffer_collection_token: Some(dup_token),
            usage: Some(ui_comp::RegisterBufferCollectionUsage::Screenshot),
            ..ui_comp::RegisterBufferCollectionArgs::EMPTY
        };

        match scenic_allocator.register_buffer_collection(args).await {
            Err(e) => {
                return Err(format_err!("RegisterBufferCollection failed with FIDL err: {}", e));
            }
            Ok(Err(val)) => {
                return Err(format_err!("RegisterBufferCollection failed with err: {:?}", val));
            }
            Ok(_) => {}
        }

        let (buffer_collection, buffer_collection_request) =
            fidl::endpoints::create_proxy::<BufferCollectionMarker>()
                .context("Failed to connect to BufferCollectionMarker")?;

        match sysmem_allocator
            .bind_shared_collection(local_clientend_token, buffer_collection_request)
        {
            Err(e) => {
                return Err(format_err!("BindSharedCollection failed with FIDL err: {}", e));
            }
            Ok(_) => {}
        }

        // Set our specific constraints on the image we want to allocate.
        //
        // The default constraints should account for the hardware these tests run on. However, we
        // must set the specific display width and height ourselves.
        let image_format_constraints = ImageFormatConstraints {
            required_min_coded_width: width,
            required_max_coded_width: width,
            required_min_coded_height: height,
            required_max_coded_height: height,
            ..IMAGE_FORMAT_CONSTRAINTS_DEFAULT
        };

        let mut constraints: BufferCollectionConstraints = BufferCollectionConstraints {
            usage: BufferUsage { none: 0, cpu: 1, vulkan: 0, display: 0, video: 0 },
            min_buffer_count: 1,
            max_buffer_count: 1,
            has_buffer_memory_constraints: true,
            buffer_memory_constraints: { BUFFER_MEMORY_CONSTRAINTS_DEFAULT },
            image_format_constraints_count: 1,
            image_format_constraints: [image_format_constraints; 32],

            min_buffer_count_for_camping: 0,
            min_buffer_count_for_dedicated_slack: 0,
            min_buffer_count_for_shared_slack: 0,
        };
        match buffer_collection.set_constraints(true, &mut constraints) {
            Err(e) => {
                return Err(format_err!("SetConstraints failed with FIDL err: {}", e));
            }
            Ok(_) => {}
        }

        let (status, scr_bc_info) = buffer_collection
            .wait_for_buffers_allocated()
            .await
            .context("Failed to wait for buffers allocated failed")?;

        if status != zx::sys::ZX_OK {
            return Err(format_err!("WaitForBuffersAllocated failed with bad status"));
        }
        buffer_collection.close()?;

        // With a valid BufferCollectionInfo, we can now tell Screenshot to render into its VMO.

        let args = ui_comp::CreateImageArgs {
            image_id: IMAGE_ID,
            import_token: Some(import_token),
            vmo_index: Some(0),
            size: Some(fidl_fuchsia_math::SizeU { width: width, height: height }),
            ..ui_comp::CreateImageArgs::EMPTY
        };
        let _ = screenshotter.create_image(args).await;

        let screenshot_done_event: fuchsia_zircon::Event = fidl::Event::create().unwrap();
        let dup_screenshot_done_event =
            screenshot_done_event.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();

        let args = ui_comp::TakeScreenshotArgs {
            image_id: IMAGE_ID,
            rotation: Some(ui_comp::Rotation::Cw0Degrees),
            event: Some(screenshot_done_event),
            ..ui_comp::TakeScreenshotArgs::EMPTY
        };

        match screenshotter.take_screenshot(args).await {
            Err(e) => {
                return Err(format_err!("Screenshot.TakeScreenshot() failed with FIDL err: {}", e));
            }
            Ok(Err(val)) => {
                return Err(format_err!("Screenshot.TakeScreenshot() failed with err: {:?}", val));
            }
            Ok(_) => {}
        }

        match dup_screenshot_done_event
            .wait_handle(zx::Signals::EVENT_SIGNALED, zx::Time::after(5.seconds()))
        {
            Err(e) => {
                return Err(format_err!(
                    "Screenshot.TakeScreenshot() event wait failed with err: {}",
                    e
                ));
            }
            Ok(_) => {}
        }

        match screenshotter
            .remove_image(ui_comp::RemoveImageArgs {
                image_id: IMAGE_ID,
                ..ui_comp::RemoveImageArgs::EMPTY
            })
            .await
        {
            Err(e) => {
                return Err(format_err!("Screenshot.RemoveImage() failed with FIDL err: {}", e));
            }
            Ok(Err(val)) => {
                return Err(format_err!("Screenshot.RemoveImage() failed with err: {:?}", val));
            }
            Ok(_) => {}
        }

        // Copy Screenshot output for inspection. Note that the stride of the buffer may be
        // different than the width of the image, if the width of the image is not a multiple of 64,
        // or some other power of 2.
        //
        // For instance, if the original image were 1024x600 and rotated 90*, then the new width is
        // 600. 600 px * 4 bytes per px = 2400 bytes, which is not a multiple of 64. The next
        // multiple would be 2432, which would mean the buffer is actually a 608x1024 "pixel"
        // buffer, since 2432/4=608. We must account for that 8*4=32 byte padding when copying the
        // bytes over to be inspected.

        let image_info = fidl_fuchsia_images::ImageInfo {
            transform: fidl_fuchsia_images::Transform::Normal,
            width: width,
            height: height,
            stride: width * 4,
            pixel_format: fidl_fuchsia_images::PixelFormat::Bgra8,
            color_space: fidl_fuchsia_images::ColorSpace::Srgb,
            tiling: fidl_fuchsia_images::Tiling::Linear,
            alpha_format: fidl_fuchsia_images::AlphaFormat::Opaque,
        };

        let image_vmo = scr_bc_info.buffers[0]
            .vmo
            .as_ref()
            .ok_or(format_err!("No VMOs returned in BufferCollectionInfo"))?
            .duplicate_handle(zx::Rights::SAME_RIGHTS)
            .unwrap();

        // Get pixels per row.
        let bytes_per_row_divisor =
            scr_bc_info.settings.image_format_constraints.bytes_per_row_divisor;
        let min_bytes_per_row = scr_bc_info.settings.image_format_constraints.min_bytes_per_row;
        let bytes_per_row =
            if min_bytes_per_row > width * 4 { min_bytes_per_row } else { width * 4 };
        let bytes_per_row = bytes_per_row + (bytes_per_row & bytes_per_row_divisor);
        let pixels_per_row = bytes_per_row / 4;

        if pixels_per_row == width {
            // We already have a packed vmo, we can return it as-is.
            let buffer = fidl_fuchsia_mem::Buffer {
                vmo: image_vmo,
                size: u64::from(scr_bc_info.settings.buffer_settings.size_bytes),
            };

            let screenshot: ScreenshotData = ScreenshotData { info: image_info, data: buffer };

            return Ok(to_value(ScreenshotDataDef::new(screenshot))?);
        } else {
            // We need to copy over the data in the VMO to a new one without 'dead bytes' at the
            // end of every row (every `stride` pixels only have `width` valid pixels).

            let packed_vmo = zx::Vmo::create((width * height * 4).into())?;

            // Copy from image_vmo to buf, and from buf to packed_vmo. Maybe we can do this more efficiently.
            let mut buf = vec![0u8; (width * 4) as usize];
            for i in 0..height {
                match image_vmo.read(&mut buf, (i * bytes_per_row).into()) {
                    Ok(_) => {}
                    Err(_) => break,
                }
                match packed_vmo.write(&mut buf, (i * (width * 4)).into()) {
                    Ok(_) => {}
                    Err(_) => break,
                }
            }

            let buffer =
                fidl_fuchsia_mem::Buffer { vmo: packed_vmo, size: u64::from(width * height * 4) };

            let screenshot: ScreenshotData = ScreenshotData { info: image_info, data: buffer };

            return Ok(to_value(ScreenshotDataDef::new(screenshot))?);
        }
    }

    pub async fn present_view(&self, url: String, config: Option<ViewConfig>) -> Result<(), Error> {
        let presenter = app::client::connect_to_protocol::<PresenterMarker>()
            .expect("Failed to connect to root presenter");

        let launcher = launcher().context("Failed to open launcher service")?;
        let app = launch(&launcher, url, None)?;

        let mut token_pair = scenic::ViewTokenPair::new()?;

        // (for now) gate v1/v2 on the presence of a view config
        match config {
            Some(mut config) => {
                // v2
                let view = app.connect_to_protocol::<ViewMarker>()?;
                view.set_config(&mut config)?;
                view.attach(token_pair.view_token.value)?;
            }
            None => {
                // v1
                let view_provider = app.connect_to_protocol::<ViewProviderMarker>()?;
                view_provider.create_view(token_pair.view_token.value, None, None)?;
            }
        }

        presenter.present_view(&mut token_pair.view_holder_token, None)?;

        app.controller().detach()?;
        Ok(())
    }
}
