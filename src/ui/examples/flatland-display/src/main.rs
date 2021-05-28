// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_scenic_allocation as scenic_alloc, fidl_fuchsia_ui_scenic_internal as fland,
    fuchsia_async as fasync, fuchsia_component::client::connect_to_protocol,
    fuchsia_syslog as syslog, fuchsia_zircon as zx, log::*,
};

use fidl::endpoints::create_proxy;
use fuchsia_framebuffer::{
    sysmem::minimum_row_bytes, sysmem::BufferCollectionAllocator, FrameUsage,
};
use std::{thread, time::Duration};

// TODO(fxbug.dev/76640): move somewhere common, like fuchsia-flatland library.
struct LinkTokenPair {
    graph_link_token: fland::GraphLinkToken,
    content_link_token: fland::ContentLinkToken,
}

impl LinkTokenPair {
    fn new() -> LinkTokenPair {
        let (raw_graph_token, raw_content_token) =
            zx::EventPair::create().expect("failed to create eventpair");
        LinkTokenPair {
            graph_link_token: fland::GraphLinkToken { value: raw_graph_token },
            content_link_token: fland::ContentLinkToken { value: raw_content_token },
        }
    }
}

// TODO(fxbug.dev/76640): move somewhere common, like fuchsia-flatland library.
struct BufferCollectionTokenPair {
    export_token: scenic_alloc::BufferCollectionExportToken,
    import_token: scenic_alloc::BufferCollectionImportToken,
}

impl BufferCollectionTokenPair {
    fn new() -> BufferCollectionTokenPair {
        let (raw_export_token, raw_import_token) =
            zx::EventPair::create().expect("failed to create eventpair");
        BufferCollectionTokenPair {
            export_token: scenic_alloc::BufferCollectionExportToken { value: raw_export_token },
            import_token: scenic_alloc::BufferCollectionImportToken { value: raw_import_token },
        }
    }
}

#[fasync::run_singlethreaded]
async fn main() {
    const IMAGE_ID: fland::ContentId = fland::ContentId { value: 2 };
    const TRANSFORM_ID: fland::TransformId = fland::TransformId { value: 3 };
    const IMAGE_WIDTH: u32 = 2;
    const IMAGE_HEIGHT: u32 = 2;
    const RECT_WIDTH: f32 = 400.0;
    const RECT_HEIGHT: f32 = 600.0;

    syslog::init_with_tags(&["flatland-display"]).expect("failed to initialize logger");

    // Connect to all three services that will be used in this example: a FlatlandDisplay which
    // displays a rooted scene graph on a physical display, a Flatland session which contains the
    // scene graph contents, and an Allocator which is used to allocate sysmem memory for the sole
    // image in the scene graph.
    let flatland_display = connect_to_protocol::<fland::FlatlandDisplayMarker>()
        .expect("error connecting to Flatland display");
    let flatland =
        connect_to_protocol::<fland::FlatlandMarker>().expect("error connecting to Flatland");
    let allocator = connect_to_protocol::<scenic_alloc::AllocatorMarker>()
        .expect("error connecting to Scenic allocator");
    info!("Established connections to Flatland and Allocator");

    // Link the Flatland display to the Flatland session.  This is accomplished by passing each of
    // them one half of the LinkTokenPair.
    let mut link_tokens = LinkTokenPair::new();

    let (_content_link_proxy, content_link_request) =
        create_proxy::<fland::ContentLinkMarker>().expect("failed to create ContentLink endpoints");
    flatland_display
        .set_content(&mut link_tokens.content_link_token, content_link_request)
        .expect("fidl error");

    let (_graph_link_proxy, graph_link_request) =
        create_proxy::<fland::GraphLinkMarker>().expect("failed to create GraphLink endpoints");
    flatland
        .link_to_parent(&mut link_tokens.graph_link_token, graph_link_request)
        .expect("fidl error");

    // BufferAllocator is a helper which makes it easier to obtain and set constraints on a
    // sysmem::BufferCollectionToken.  This token can then be registered with Scenic, which will
    // set its own constraints; see below.
    let mut buffer_allocator = BufferCollectionAllocator::new(
        IMAGE_WIDTH,
        IMAGE_HEIGHT,
        fidl_fuchsia_sysmem::PixelFormatType::Bgra32,
        FrameUsage::Cpu,
        1,
    )
    .expect("failed to create BufferCollectionAllocator");

    buffer_allocator.set_name(100, "FlatlandDisplayFramebuffer").expect("fidl error");
    let sysmem_buffer_collection_token =
        buffer_allocator.duplicate_token().await.expect("error duplicating token");

    // Register the sysmem BufferCollectionToken with the Scenic Allocator API.  This is done by
    // creating an import/export token pair, which is fundamentally a pair of zx::event.  The export
    // token is used as a key to register the sysmem BufferCollectionToken.  The corresponding
    // import token can be used to access the allocated buffers via other Scenic APIs, such as the
    // "Gfx" and "Flatland" APIs, the latter being used in this example.  See below:
    // flatland.create_image().
    let mut buffer_tokens = BufferCollectionTokenPair::new();
    let args = fidl_fuchsia_scenic_allocation::RegisterBufferCollectionArgs {
        export_token: Some(buffer_tokens.export_token),
        buffer_collection_token: Some(sysmem_buffer_collection_token),
        ..fidl_fuchsia_scenic_allocation::RegisterBufferCollectionArgs::EMPTY
    };

    allocator
        .register_buffer_collection(args)
        .await
        .expect("fidl error")
        .expect("error registering buffer collection");

    // Now that the BufferCollectionToken has been registered, Scenic is able to set constraints on
    // it so that the eventually-allocated buffer can be used by e.g. both Vulkan and the hardware
    // display controller.  Allocate the buffer and wait for the allocation to finish, which cannot
    // happen until Scenic has set all necessary constraints of its own.
    let allocation =
        buffer_allocator.allocate_buffers(true).await.expect("buffer allocation failed");

    // Write pixel values into the allocated buffer.
    match &allocation.buffers[0].vmo {
        Some(vmo) => {
            assert!(IMAGE_WIDTH == 2);
            assert!(IMAGE_HEIGHT == 2);

            // Compute the same row-pitch as Flatland will compute internally.
            assert!(allocation.settings.has_image_format_constraints);
            let row_pitch: u64 =
                minimum_row_bytes(allocation.settings.image_format_constraints, IMAGE_WIDTH)
                    .expect("failed to compute row-pitch")
                    .into();

            // TODO(fxbug.dev/76640): should look at pixel-format, instead of assuming 32-bit BGRA
            // pixels.  For now, format is hard-coded anyway.
            let fuchsia_pixel: [u8; 4] = [255, 0, 255, 255];
            let white_pixel: [u8; 4] = [255, 255, 255, 255];
            let blue_pixel: [u8; 4] = [255, 0, 0, 255];
            let red_pixel: [u8; 4] = [0, 0, 255, 255];

            let offset = allocation.buffers[0].vmo_usable_start;

            vmo.write(&fuchsia_pixel, offset).expect("failed to write pixel");
            vmo.write(&white_pixel, offset + 4).expect("failed to write pixel");
            vmo.write(&blue_pixel, offset + row_pitch).expect("failed to write pixel");
            vmo.write(&red_pixel, offset + row_pitch + 4).expect("failed to write pixel");
        }
        None => unreachable!(),
    }

    // Create an image in the Flatland session, using the sysmem buffer we just allocated.
    // As mentioned above, this uses the import token corresponding to the export token that was
    // used to register the BufferCollectionToken with the Scenic Allocator.
    let image_props = fland::ImageProperties {
        width: Some(IMAGE_WIDTH),
        height: Some(IMAGE_HEIGHT),
        ..fland::ImageProperties::EMPTY
    };
    // TODO(fxbug.dev/76640): generated FIDL methods currently expect "&mut" args.  fxbug.dev/65845
    // is changing the generated FIDL to use "&" instead (at lease for POD structs like these); when
    // this lands we can remove the ".clone()" from the call sites below.
    flatland
        .create_image(&mut IMAGE_ID.clone(), &mut buffer_tokens.import_token, 0, image_props)
        .expect("fidl error");

    // Populate the rest of the Flatland scene.  There is a single transform which is set as the
    // root transform; the newly-created image is set as the content of that transform.
    flatland.create_transform(&mut TRANSFORM_ID.clone()).expect("fidl error");
    flatland
        .set_scale(&mut TRANSFORM_ID.clone(), &mut fland::Vec2 { x: RECT_WIDTH, y: RECT_HEIGHT })
        .expect("fidl error");
    flatland.set_root_transform(&mut TRANSFORM_ID.clone()).expect("fidl error");
    flatland.set_content(&mut TRANSFORM_ID.clone(), &mut IMAGE_ID.clone()).expect("fidl error");

    // Now that the Flatland session is linked to the FlatlandDisplay, and the session's scene has
    // been populated, we can present the session in order to display everything on-screen.
    let args = fland::PresentArgs {
        requested_presentation_time: Some(0),
        acquire_fences: None,
        release_fences: None,
        squashable: Some(false),
        ..fland::PresentArgs::EMPTY
    };
    flatland.present(args).await.expect("fidl error").expect("present failed");

    // TODO(fxbug.dev/76640): give Scenic enough time to render a frame before killing the session;
    // if we don't, then the content will never appear on the screen.  Worse, though, is that if we
    // remove this sleep and run the example twice in a row, we hit a DCHECK because there is a gap
    // in the frame numbers passed to flatland::Engine::RenderScheduledFrame().  This needs further
    // investigation, because it's difficult to reason about all the things that might go wrong as
    // a result.
    thread::sleep(Duration::from_millis(100));
}
