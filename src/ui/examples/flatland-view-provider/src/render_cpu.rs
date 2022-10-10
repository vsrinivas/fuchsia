// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::render::Renderer,
    fidl_fuchsia_sysmem as fsysmem, fidl_fuchsia_ui_composition as fland,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_framebuffer::{
        sysmem::{minimum_row_bytes, BufferCollectionAllocator},
        FrameUsage,
    },
    fuchsia_scenic::{duplicate_buffer_collection_import_token, BufferCollectionTokenPair},
    fuchsia_zircon as zx,
};

pub struct CpuRenderer {
    import_token: fland::BufferCollectionImportToken,
    buffer_info: fsysmem::BufferCollectionInfo2,
    row_pitch: usize,
    page_size: usize,
}

const IMAGE_WIDTH: u32 = 2;
const IMAGE_HEIGHT: u32 = 2;
// TODO(fxbug.dev/104692): it would be better to let sysmem choose between several pixel formats.
const PIXEL_FORMAT: fsysmem::PixelFormatType = fsysmem::PixelFormatType::Bgra32;

impl Renderer for CpuRenderer {
    fn duplicate_buffer_collection_import_token(&self) -> fland::BufferCollectionImportToken {
        duplicate_buffer_collection_import_token(&self.import_token).unwrap()
    }

    fn render_rgba(&self, buffer_index: usize, colors: [[u8; 4]; 4]) {
        let single_buffer_settings = &self.buffer_info.settings;
        let vmo_info = &self.buffer_info.buffers[buffer_index];

        if let Some(vmo) = &vmo_info.vmo {
            // The size used to map a VMO must be a multiple of the page size.  Ensure that the
            // VMO is at least one page in size, and that the size returned by sysmem is no
            // larger than this.  Neither of these should ever fail.
            {
                let vmo_size: usize =
                    vmo.get_size().expect("failed to obtain VMO size").try_into().unwrap();
                let sysmem_size: usize =
                    single_buffer_settings.buffer_settings.size_bytes.try_into().unwrap();
                assert!(self.page_size <= vmo_size);
                assert!(self.page_size >= sysmem_size);
            }

            // create_from_vmo() uses an offset of 0 when mapping the VMO; verify that this
            // matches the sysmem allocation.
            let offset: usize = vmo_info.vmo_usable_start.try_into().unwrap();
            assert_eq!(offset, 0);

            // These mappings could be cached if profiling showed a benefit.
            // See `SharedBuffer::release_writes()` in `//src/lib/shared-buffer/src/lib.rs`.
            let mapping = mapped_vmo::Mapping::create_from_vmo(
                &vmo,
                self.page_size,
                zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
            )
            .expect("failed to map VMO");

            // TODO(fxbug.dev/104692): we're smashing RGBA pixels into a BGRA-format image, oops.
            // Given the desired aesthetic (cycling through the color wheel), it's unimportant.
            mapping.write_at(0, &colors[0]);
            mapping.write_at(4, &colors[1]);
            mapping.write_at(self.row_pitch, &colors[2]);
            mapping.write_at(self.row_pitch + 4, &colors[3]);
        } else {
            unreachable!();
        }
    }
}

impl CpuRenderer {
    async fn allocate_buffers(
        pixel_format: fsysmem::PixelFormatType,
        width: u32,
        height: u32,
        image_count: usize,
    ) -> (fsysmem::BufferCollectionInfo2, fland::BufferCollectionImportToken) {
        // BufferAllocator is a helper which makes it easier to obtain and set constraints on a
        // sysmem::BufferCollectionToken.  This token can then be registered with Scenic, which will
        // set its own constraints; see below.
        let mut allocator_helper = BufferCollectionAllocator::new(
            width,
            height,
            pixel_format,
            FrameUsage::Cpu,
            image_count,
        )
        .expect("failed to create BufferCollectionAllocator");
        allocator_helper.set_name(100, "Flatland ViewProvider Example").expect("fidl error");

        let sysmem_buffer_collection_token =
            allocator_helper.duplicate_token().await.expect("error duplicating token");

        // Register the sysmem BufferCollectionToken with the Scenic Allocator API.  This is done by
        // creating an import/export token pair, which is fundamentally a pair of zx::event.  The
        // export token is used as a key to register the sysmem BufferCollectionToken.  The
        // corresponding import token can be used to access the allocated buffers via other Scenic
        // APIs, such as the "Gfx" and "Flatland" APIs, the latter being used in this example.  See
        // the following invocation of "flatland.create_image()".
        //
        let buffer_tokens = BufferCollectionTokenPair::new();
        let args = fland::RegisterBufferCollectionArgs {
            export_token: Some(buffer_tokens.export_token),
            buffer_collection_token: Some(sysmem_buffer_collection_token),
            ..fland::RegisterBufferCollectionArgs::EMPTY
        };

        let scenic_allocator = connect_to_protocol::<fland::AllocatorMarker>()
            .expect("error connecting to Scenic allocator");
        scenic_allocator
            .register_buffer_collection(args)
            .await
            .expect("fidl error")
            .expect("error registering buffer collection");

        // Now that the BufferCollectionToken has been registered, Scenic is able to set constraints
        // on it so that the eventually-allocated buffer can be used by e.g. both Vulkan and the
        // hardware display controller.  Allocate the buffer and wait for the allocation to finish,
        // which cannot happen until Scenic has set all necessary constraints of its own.
        let buffer_collection_info =
            allocator_helper.allocate_buffers(true).await.expect("buffer allocation failed");

        (buffer_collection_info, buffer_tokens.import_token)
    }

    pub async fn new(width: u32, height: u32, image_count: usize) -> CpuRenderer {
        assert!(width == IMAGE_WIDTH);
        assert!(height == IMAGE_HEIGHT);

        let (buffer_collection_info, import_token) =
            CpuRenderer::allocate_buffers(PIXEL_FORMAT, width, height, image_count).await;

        let single_buffer_settings = &buffer_collection_info.settings;

        // Compute the same row-pitch as Flatland will compute internally.
        assert!(single_buffer_settings.has_image_format_constraints);
        let row_pitch: usize =
            minimum_row_bytes(single_buffer_settings.image_format_constraints, width)
                .expect("failed to compute row-pitch")
                .try_into()
                .unwrap();

        let page_size = zx::system_get_page_size().try_into().unwrap();

        CpuRenderer { import_token, buffer_info: buffer_collection_info, row_pitch, page_size }
    }
}
