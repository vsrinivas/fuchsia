// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::hanging_get::client::HangingGetStream,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_math as fmath, fidl_fuchsia_sysmem as fsysmem, fidl_fuchsia_ui_app as fapp,
    fidl_fuchsia_ui_composition as fland, fidl_fuchsia_ui_views as fviews, fuchsia_async as fasync,
    fuchsia_component::{self as component, client::connect_to_protocol},
    fuchsia_framebuffer::{
        sysmem::minimum_row_bytes, sysmem::BufferCollectionAllocator, FrameUsage,
    },
    fuchsia_scenic::{flatland, BufferCollectionTokenPair, ViewRefPair},
    fuchsia_trace as trace, fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{unbounded, UnboundedSender},
        future,
        prelude::*,
    },
    log::*,
    std::convert::TryInto,
};

const IMAGE_ID: fland::ContentId = fland::ContentId { value: 2 };
const TRANSFORM_ID: fland::TransformId = fland::TransformId { value: 3 };
const IMAGE_WIDTH: u32 = 2;
const IMAGE_HEIGHT: u32 = 2;

fn pos_mod(arg: f32, modulus: f32) -> f32 {
    assert!(modulus > 0.0);
    let mut result = arg % modulus;
    if result < 0.0 {
        result += modulus;
    }
    result
}

fn hsv_to_rgba(h: f32, s: f32, v: f32) -> [u8; 4] {
    assert!(s <= 100.0);
    assert!(v <= 100.0);
    let h = pos_mod(h, 360.0);

    let c = v / 100.0 * s / 100.0;
    let x = c * (1.0 - (((h / 60.0) % 2.0) - 1.0).abs());
    let m = (v / 100.0) - c;

    let (mut r, mut g, mut b) = match h {
        h if h < 60.0 => (c, x, 0.0),
        h if h < 120.0 => (x, c, 0.0),
        h if h < 180.0 => (0.0, c, x),
        h if h < 240.0 => (0.0, x, c),
        h if h < 300.0 => (x, 0.0, c),
        _ => (c, 0.0, x),
    };

    r += m;
    g += m;
    b += m;

    return [(r * 255.0) as u8, (g * 255.0) as u8, (b * 255.0) as u8, 255];
}

enum MessageInternal {
    CreateView(fviews::ViewCreationToken, fviews::ViewRefControl, fviews::ViewRef),
    OnPresentError {
        error: fland::FlatlandError,
    },
    OnNextFrameBegin {
        additional_present_credits: u32,
        future_presentation_infos: Vec<flatland::PresentationInfo>,
    },
    Relayout {
        width: u32,
        height: u32,
    },
}

struct AppModel {
    flatland: fland::FlatlandProxy,
    allocator: fland::AllocatorProxy,
    internal_sender: UnboundedSender<MessageInternal>,
    allocation: Option<fsysmem::BufferCollectionInfo2>,
    num_presents_allowed: u32,
    pending_present: bool,
    present_count: u64,
    hue: f32,
    page_size: usize,
}

impl AppModel {
    fn new(
        flatland: fland::FlatlandProxy,
        allocator: fland::AllocatorProxy,
        internal_sender: UnboundedSender<MessageInternal>,
    ) -> AppModel {
        AppModel {
            flatland,
            allocator,
            internal_sender,
            allocation: None,
            num_presents_allowed: 1,
            pending_present: false,
            present_count: 0,
            hue: 0.0,
            page_size: zx::system_get_page_size().try_into().unwrap(),
        }
    }

    async fn init_scene(&mut self) {
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

        buffer_allocator.set_name(100, "Flatland ViewProvider Example").expect("fidl error");
        let sysmem_buffer_collection_token =
            buffer_allocator.duplicate_token().await.expect("error duplicating token");

        // Register the sysmem BufferCollectionToken with the Scenic Allocator API.  This is done by
        // creating an import/export token pair, which is fundamentally a pair of zx::event.  The
        // export token is used as a key to register the sysmem BufferCollectionToken.  The
        // corresponding import token can be used to access the allocated buffers via other Scenic
        // APIs, such as the "Gfx" and "Flatland" APIs, the latter being used in this example.  See
        // the following invocation of "flatland.create_image()".
        let mut buffer_tokens = BufferCollectionTokenPair::new();
        let args = fland::RegisterBufferCollectionArgs {
            export_token: Some(buffer_tokens.export_token),
            buffer_collection_token: Some(sysmem_buffer_collection_token),
            ..fland::RegisterBufferCollectionArgs::EMPTY
        };

        self.allocator
            .register_buffer_collection(args)
            .await
            .expect("fidl error")
            .expect("error registering buffer collection");

        // Now that the BufferCollectionToken has been registered, Scenic is able to set constraints
        // on it so that the eventually-allocated buffer can be used by e.g. both Vulkan and the
        // hardware display controller.  Allocate the buffer and wait for the allocation to finish,
        // which cannot happen until Scenic has set all necessary constraints of its own.
        self.allocation =
            Some(buffer_allocator.allocate_buffers(true).await.expect("buffer allocation failed"));

        self.set_image_colors();

        // Create an image in the Flatland session, using the sysmem buffer we just allocated.
        // As mentioned above, this uses the import token corresponding to the export token that was
        // used to register the BufferCollectionToken with the Scenic Allocator.
        let image_props = fland::ImageProperties {
            size: Some(fmath::SizeU { width: IMAGE_WIDTH, height: IMAGE_HEIGHT }),
            ..fland::ImageProperties::EMPTY
        };
        // TODO(fxbug.dev/76640): generated FIDL methods currently expect "&mut" args.  This will
        // change; according to fxbug.dev/65845 the generated FIDL will use "&" instead (at least
        // for POD structs like these).  When this lands we can remove the ".clone()" from the call
        //  sites below.
        self.flatland
            .create_image(&mut IMAGE_ID.clone(), &mut buffer_tokens.import_token, 0, image_props)
            .expect("fidl error");

        // Populate the rest of the Flatland scene.  There is a single transform which is set as the
        // root transform; the newly-created image is set as the content of that transform.
        self.flatland.create_transform(&mut TRANSFORM_ID.clone()).expect("fidl error");
        self.flatland.set_root_transform(&mut TRANSFORM_ID.clone()).expect("fidl error");
        self.flatland
            .set_content(&mut TRANSFORM_ID.clone(), &mut IMAGE_ID.clone())
            .expect("fidl error");
    }

    fn create_parent_viewport_watcher(
        &mut self,
        mut view_creation_token: fviews::ViewCreationToken,
    ) {
        let (parent_viewport_watcher, server_end) =
            create_proxy::<fland::ParentViewportWatcherMarker>()
                .expect("failed to create ParentViewportWatcherProxy");

        // NOTE: it isn't necessary to call maybe_present() for this to take effect, because we will
        // relayout when receive the initial layout info.  See CreateView() FIDL docs.
        self.flatland.create_view(&mut view_creation_token, server_end).expect("fidl error");

        // NOTE: there may be a race condition if TemporaryFlatlandViewProvider.CreateView() is
        // invoked a second time, causing us to create another graph link.  Because Zircon doesn't
        // guarantee ordering on responses of different channels, we might receive data from the old
        // link after data from the new link, just before the old link is closed.  Non-example code
        // should be more careful (this assumes that the client expects CreateView() to be called
        // multiple times, which clients commonly don't).
        let sender = self.internal_sender.clone();
        fasync::Task::spawn(async move {
            let mut layout_info_stream =
                HangingGetStream::new(Box::new(move || Some(parent_viewport_watcher.get_layout())));

            while let Some(result) = layout_info_stream.next().await {
                match result {
                    Ok(layout_info) => {
                        let mut width = 0;
                        let mut height = 0;
                        if let Some(logical_size) = layout_info.logical_size {
                            width = logical_size.width;
                            height = logical_size.height;
                        }
                        sender
                            .unbounded_send(MessageInternal::Relayout { width, height })
                            .expect("failed to send MessageInternal.");
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
                        info!("graph link connection closed.");
                        return; // from spawned task closure
                    }
                    Err(fidl_error) => {
                        warn!("graph link GetLayout() error: {:?}", fidl_error);
                        return; // from spawned task closure
                    }
                }
            }
        })
        .detach();
    }

    fn on_present_processed(
        &mut self,
        additional_present_credits: u32,
        _future_presentation_infos: Vec<flatland::PresentationInfo>,
    ) {
        trace::duration!("gfx", "FlatlandViewProvider::on_present_processed");
        self.num_presents_allowed += additional_present_credits;

        self.hue = (self.hue + 0.5) % 360.0;
        self.set_image_colors();
        self.pending_present = true;

        self.maybe_present();
    }

    fn on_relayout(&mut self, width: u32, height: u32) {
        self.flatland
            .set_image_destination_size(&mut IMAGE_ID.clone(), &mut fmath::SizeU { width, height })
            .expect("fidl error");
        self.pending_present = true;
        self.maybe_present();
    }

    fn maybe_present(&mut self) {
        if self.num_presents_allowed > 0 && self.pending_present {
            trace::duration!("gfx", "FlatlandViewProvider::Present()");
            trace::flow_begin!("gfx", "Flatland::Present", self.present_count);
            self.present_count += 1;
            self.pending_present = false;
            self.num_presents_allowed -= 1;
            self.flatland
                .present(fland::PresentArgs {
                    requested_presentation_time: Some(0),
                    acquire_fences: None,
                    release_fences: None,
                    unsquashable: Some(true),
                    ..fland::PresentArgs::EMPTY
                })
                .expect("fidl error");
        }
    }

    fn set_image_colors(&mut self) {
        let allocation = self.allocation.as_ref().unwrap();

        // Write pixel values into the allocated buffer.
        match &allocation.buffers[0].vmo {
            Some(vmo) => {
                assert!(IMAGE_WIDTH == 2);
                assert!(IMAGE_HEIGHT == 2);

                // Compute the same row-pitch as Flatland will compute internally.
                assert!(allocation.settings.has_image_format_constraints);
                let row_pitch: usize =
                    minimum_row_bytes(allocation.settings.image_format_constraints, IMAGE_WIDTH)
                        .expect("failed to compute row-pitch")
                        .try_into()
                        .unwrap();

                // TODO(fxbug.dev/76640): should look at pixel-format, instead of assuming 32-bit
                // BGRA pixels.  For now, format is hard-coded anyway.
                let p00: [u8; 4] = hsv_to_rgba(self.hue, 30.0, 75.0);
                let p10: [u8; 4] = hsv_to_rgba(self.hue + 20.0, 30.0, 75.0);
                let p11: [u8; 4] = hsv_to_rgba(self.hue + 60.0, 30.0, 75.0);
                let p01: [u8; 4] = hsv_to_rgba(self.hue + 40.0, 30.0, 75.0);

                // The size used to map a VMO must be a multiple of the page size.  Ensure that the
                // VMO is at least one page in size, and that the size returned by sysmem is no
                // larger than this.  Neither of these should ever fail.
                {
                    let vmo_size: usize =
                        vmo.get_size().expect("failed to obtain VMO size").try_into().unwrap();
                    let sysmem_size: usize =
                        allocation.settings.buffer_settings.size_bytes.try_into().unwrap();
                    assert!(self.page_size <= vmo_size);
                    assert!(self.page_size >= sysmem_size);
                }

                // create_from_vmo() uses an offset of 0 when mapping the VMO; verify that this
                // matches the sysmem allocation.
                let offset: usize = allocation.buffers[0].vmo_usable_start.try_into().unwrap();
                assert_eq!(offset, 0);

                let mapping = mapped_vmo::Mapping::create_from_vmo(
                    &vmo,
                    self.page_size,
                    zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
                )
                .expect("failed to map VMO");

                mapping.write_at(0, &p00);
                mapping.write_at(4, &p10);
                mapping.write_at(row_pitch, &p01);
                mapping.write_at(row_pitch + 4, &p11);
            }
            None => unreachable!(),
        }
    }
}

fn setup_fidl_services(sender: UnboundedSender<MessageInternal>) {
    let view_provider_cb = move |stream: fapp::ViewProviderRequestStream| {
        let sender = sender.clone();
        fasync::Task::local(
            stream
                .try_for_each(move |req| {
                    match req {
                        fapp::ViewProviderRequest::CreateView2 { args, .. } => {
                            let view_creation_token = args.view_creation_token.unwrap();
                            // We do not get passed a view ref so create our own.
                            let ViewRefPair { control_ref, view_ref } =
                                ViewRefPair::new().expect("unable to create view ref pair");

                            sender
                                .unbounded_send(MessageInternal::CreateView(
                                    view_creation_token,
                                    control_ref,
                                    view_ref,
                                ))
                                .expect("failed to send MessageInternal.");
                        }
                        unhandled_req => {
                            warn!("Unhandled ViewProvider request: {:?}", unhandled_req);
                        }
                    };
                    future::ok(())
                })
                .unwrap_or_else(|e| {
                    eprintln!("error running TemporaryFlatlandViewProvider server: {:?}", e)
                }),
        )
        .detach()
    };

    let mut fs = component::server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(view_provider_cb);

    fs.take_and_serve_directory_handle().expect("failed to serve directory handle");
    fasync::Task::local(fs.collect()).detach();
}

fn setup_handle_flatland_events(
    event_stream: fland::FlatlandEventStream,
    sender: UnboundedSender<MessageInternal>,
) {
    fasync::Task::local(
        event_stream
            .try_for_each(move |event| {
                match event {
                    fland::FlatlandEvent::OnNextFrameBegin { values } => {
                        if let (Some(additional_present_credits), Some(future_presentation_infos)) =
                            (values.additional_present_credits, values.future_presentation_infos)
                        {
                            sender
                                .unbounded_send(MessageInternal::OnNextFrameBegin {
                                    additional_present_credits,
                                    future_presentation_infos,
                                })
                                .expect("failed to send MessageInternal");
                        } else {
                            // If not an error, all table fields are guaranteed to be present.
                            unreachable!()
                        }
                    }
                    fland::FlatlandEvent::OnFramePresented { frame_presented_info: _ } => {
                        trace::duration!("gfx", "FlatlandViewProvider::OnFramePresented");
                        // For simplicity, we are ignoring this event and driving our endless
                        // animation via OnNextFrameBegin.  A more advanced app might make use of
                        // the info here to compute the animation values, based on the predicted
                        // future presentation times.  For example, if Scenic decides to schedule
                        // this app at 30fps instead of 60fps, the color cycling will slow down.
                        // Note that if the app chooses to use the info in OnFramePresented, it must
                        // still handle OnNextFrameBegin to receive additional_present_credits.
                    }
                    fland::FlatlandEvent::OnError { error } => {
                        sender
                            .unbounded_send(MessageInternal::OnPresentError { error })
                            .expect("failed to send MessageInternal.");
                    }
                };
                future::ok(())
            })
            .unwrap_or_else(|e| eprintln!("error listening for Flatland Events: {:?}", e)),
    )
    .detach();
}

#[fasync::run_singlethreaded]
async fn main() {
    fuchsia_trace_provider::trace_provider_create_with_fdio();
    fuchsia_syslog::init_with_tags(&["flatland-display"]).expect("failed to initialize logger");

    let (internal_sender, mut internal_receiver) = unbounded::<MessageInternal>();

    let flatland =
        connect_to_protocol::<fland::FlatlandMarker>().expect("error connecting to Flatland");

    let allocator = connect_to_protocol::<fland::AllocatorMarker>()
        .expect("error connecting to Scenic allocator");
    info!("Established connections to Flatland and Allocator");

    setup_fidl_services(internal_sender.clone());
    setup_handle_flatland_events(flatland.take_event_stream(), internal_sender.clone());

    let mut app = AppModel::new(flatland, allocator, internal_sender.clone());
    app.init_scene().await;

    while let Some(message) = internal_receiver.next().await {
        match message {
            MessageInternal::CreateView(view_creation_token, _view_ref_control, _view_ref) => {
                // TODO(fxbug.dev/78866): handling ViewRefs is necessary for focus management.
                // For now, input is unsupported, and so we drop the ViewRef and ViewRefControl.
                app.create_parent_viewport_watcher(view_creation_token);
            }
            MessageInternal::Relayout { width, height } => {
                app.on_relayout(width, height);
            }
            MessageInternal::OnPresentError { error } => {
                error!("OnPresentError({:?})", error);
                break;
            }
            MessageInternal::OnNextFrameBegin {
                additional_present_credits,
                future_presentation_infos,
            } => app.on_present_processed(additional_present_credits, future_presentation_infos),
        }
    }
}
