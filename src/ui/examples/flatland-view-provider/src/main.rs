// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod ash_extensions;
mod internal_message;
mod mouse;
mod render;
mod render_cpu;
mod render_vk;
mod touch;

use {
    crate::{render::Renderer, render_cpu::CpuRenderer, render_vk::VulkanRenderer},
    argh::FromArgs,
    async_utils::hanging_get::client::HangingGetStream,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_math as fmath, fidl_fuchsia_ui_app as fapp, fidl_fuchsia_ui_composition as fland,
    fidl_fuchsia_ui_pointer as fptr, fidl_fuchsia_ui_views as fviews,
    flatland_frame_scheduling_lib::*,
    fuchsia_async as fasync,
    fuchsia_component::{self as component, client::connect_to_protocol},
    fuchsia_framebuffer::FrameUsage,
    fuchsia_scenic::ViewRefPair,
    fuchsia_trace as trace, fuchsia_zircon as zx,
    futures::{
        channel::mpsc::{unbounded, UnboundedSender},
        future,
        prelude::*,
    },
    internal_message::*,
    std::ops::DerefMut,
    tracing::{error, info, warn},
};

const IMAGE_COUNT: usize = 3;
const IMAGE_IDS: [fland::ContentId; IMAGE_COUNT] =
    [fland::ContentId { value: 2 }, fland::ContentId { value: 3 }, fland::ContentId { value: 4 }];
const TRANSFORM_ID: fland::TransformId = fland::TransformId { value: 5 };
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

#[derive(Clone, Debug, FromArgs)]
/// Simple Rust implementation of a view component.
pub struct Args {
    /// use Vulkan, or CPU rendering.
    #[argh(switch)]
    #[allow(unused)]
    use_vulkan: bool,
}

struct AppModel<'a> {
    flatland: &'a fland::FlatlandProxy,
    internal_sender: UnboundedSender<InternalMessage>,
    sched_lib: &'a dyn SchedulingLib,
    hue: f32,
    last_expected_presentation_time: zx::Time,
    is_focused: bool,
    frame_count: usize,
}

impl<'a> AppModel<'a> {
    fn new(
        flatland: &'a fland::FlatlandProxy,
        internal_sender: UnboundedSender<InternalMessage>,
        sched_lib: &'a dyn SchedulingLib,
    ) -> AppModel<'a> {
        AppModel {
            flatland,
            internal_sender,
            sched_lib,
            hue: 0.0,
            // If there are multiple instances of this example on-screen, it looks prettier if they
            // don't all have exactly the same color, which would happen if we zeroed this value.
            last_expected_presentation_time: zx::Time::get_monotonic(),
            is_focused: false,
            frame_count: 0,
        }
    }

    async fn init_scene(&mut self, frame_usage: FrameUsage) -> Box<dyn Renderer> {
        let renderer: Box<dyn Renderer> = match frame_usage {
            FrameUsage::Cpu => {
                Box::new(CpuRenderer::new(IMAGE_WIDTH, IMAGE_HEIGHT, IMAGE_COUNT).await)
            }
            FrameUsage::Gpu => {
                Box::new(VulkanRenderer::new(IMAGE_WIDTH, IMAGE_HEIGHT, IMAGE_COUNT).await)
            }
        };

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
        for image_index in 0..IMAGE_COUNT {
            self.flatland
                .create_image(
                    &mut IMAGE_IDS[image_index].clone(),
                    &mut renderer.duplicate_buffer_collection_import_token(),
                    image_index as u32,
                    image_props.clone(),
                )
                .expect("fidl error");

            // The rendered pixels are opaque, so don't waste time blending them.
            self.flatland
                .set_image_blending_function(
                    &mut IMAGE_IDS[image_index].clone(),
                    fland::BlendMode::Src,
                )
                .expect("fidl error");
        }

        // Populate the rest of the Flatland scene.  There is a single transform which is set as the
        // root transform; the newly-created image is set as the content of that transform.
        self.flatland.create_transform(&mut TRANSFORM_ID.clone()).expect("fidl error");
        self.flatland.set_root_transform(&mut TRANSFORM_ID.clone()).expect("fidl error");
        self.flatland
            .set_content(&mut TRANSFORM_ID.clone(), &mut IMAGE_IDS[0].clone())
            .expect("fidl error");

        renderer
    }

    fn create_parent_viewport_watcher(
        &mut self,
        mut view_creation_token: fviews::ViewCreationToken,
        mut view_identity: fviews::ViewIdentityOnCreation,
    ) {
        let (parent_viewport_watcher, parent_viewport_watcher_request) =
            create_proxy::<fland::ParentViewportWatcherMarker>()
                .expect("failed to create ParentViewportWatcherProxy");
        let (focused, focused_request) = create_proxy::<fviews::ViewRefFocusedMarker>()
            .expect("failed to create ViewRefFocusedProxy");
        let (touch, touch_request) =
            create_proxy::<fptr::TouchSourceMarker>().expect("failed to create TouchSource");
        let (mouse, mouse_request) =
            create_proxy::<fptr::MouseSourceMarker>().expect("failed to create MouseSource");

        let view_bound_protocols = fland::ViewBoundProtocols {
            view_ref_focused: Some(focused_request),
            touch_source: Some(touch_request),
            mouse_source: Some(mouse_request),
            ..fland::ViewBoundProtocols::EMPTY
        };

        // NOTE: it isn't necessary to call maybe_present() for this to take effect, because we will
        // relayout when receive the initial layout info.  See CreateView() FIDL docs.
        self.flatland
            .create_view2(
                &mut view_creation_token,
                &mut view_identity,
                view_bound_protocols,
                parent_viewport_watcher_request,
            )
            .expect("fidl error");

        Self::spawn_layout_info_watcher(parent_viewport_watcher, self.internal_sender.clone());
        Self::spawn_view_ref_focused_watcher(focused, self.internal_sender.clone());
        touch::spawn_touch_source_watcher(touch, self.internal_sender.clone());
        mouse::spawn_mouse_source_watcher(mouse, self.internal_sender.clone());
    }

    fn spawn_layout_info_watcher(
        parent_viewport_watcher: fland::ParentViewportWatcherProxy,
        sender: UnboundedSender<InternalMessage>,
    ) {
        // NOTE: there may be a race condition if TemporaryFlatlandViewProvider.CreateView() is
        // invoked a second time, causing us to create another graph link.  Because Zircon doesn't
        // guarantee ordering on responses of different channels, we might receive data from the old
        // link after data from the new link, just before the old link is closed.  Non-example code
        // should be more careful (this assumes that the client expects CreateView() to be called
        // multiple times, which clients commonly don't).
        fasync::Task::spawn(async move {
            let mut layout_info_stream = HangingGetStream::new(
                parent_viewport_watcher,
                fland::ParentViewportWatcherProxy::get_layout,
            );

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
                            .unbounded_send(InternalMessage::Relayout { width, height })
                            .expect("failed to send InternalMessage.");
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
                        info!("ParentViewportWatcher connection closed.");
                        return; // from spawned task closure
                    }
                    Err(fidl_error) => {
                        warn!("ParentViewportWatcher GetLayout() error: {:?}", fidl_error);
                        return; // from spawned task closure
                    }
                }
            }
        })
        .detach();
    }

    fn spawn_view_ref_focused_watcher(
        focused: fviews::ViewRefFocusedProxy,
        sender: UnboundedSender<InternalMessage>,
    ) {
        fasync::Task::spawn(async move {
            let mut focused_stream =
                HangingGetStream::new(focused, fviews::ViewRefFocusedProxy::watch);
            while let Some(result) = focused_stream.next().await {
                match result {
                    Ok(fviews::FocusState { focused: Some(focused), .. }) => {
                        sender
                            .unbounded_send(InternalMessage::FocusChanged { is_focused: focused })
                            .expect("failed to send InternalMessage.");
                    }
                    Ok(_) => {
                        error!("Missing required field FocusState.focused");
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
                        info!("ViewRefFocused connection closed.");
                        return; // from spawned task closure
                    }
                    Err(fidl_error) => {
                        warn!("ViewRefFocused GetLayout() error: {:?}", fidl_error);
                        return; // from spawned task closure
                    }
                }
            }
        })
        .detach();
    }

    fn draw(&mut self, expected_presentation_time: zx::Time, renderer: &mut dyn Renderer) {
        trace::duration!("gfx", "FlatlandViewProvider::draw");

        self.frame_count += 1;
        let buffer_index = self.frame_count % IMAGE_COUNT;

        let time_since_last_draw_in_seconds = ((expected_presentation_time.into_nanos()
            - self.last_expected_presentation_time.into_nanos())
            as f32)
            / 1_000_000_000.0;
        self.last_expected_presentation_time = expected_presentation_time;
        let hue_change_time_per_second = 30 as f32;
        self.hue =
            (self.hue + hue_change_time_per_second * time_since_last_draw_in_seconds) % 360.0;

        // NOTE: these colors are opaque (alpha = 1.0), which is good because elsewhere we use SRC
        // blend mode instead of SRC_OVER.
        let saturation = if self.is_focused { 75.0 } else { 30.0 };
        let colors = [
            hsv_to_rgba(self.hue, saturation, 75.0),
            hsv_to_rgba(self.hue + 20.0, saturation, 75.0),
            hsv_to_rgba(self.hue + 40.0, saturation, 75.0),
            hsv_to_rgba(self.hue + 60.0, saturation, 75.0),
        ];

        renderer.render_rgba(buffer_index, colors);

        self.flatland
            .set_content(&mut TRANSFORM_ID.clone(), &mut IMAGE_IDS[buffer_index].clone())
            .expect("fidl error");

        self.sched_lib.request_present();
    }

    fn on_relayout(&mut self, width: u32, height: u32) {
        for &id in IMAGE_IDS.iter() {
            self.flatland
                .set_image_destination_size(&mut id.clone(), &mut fmath::SizeU { width, height })
                .expect("fidl error");
        }
        self.sched_lib.request_present();
    }
}

fn setup_fidl_services(sender: UnboundedSender<InternalMessage>) {
    let view_provider_cb = move |stream: fapp::ViewProviderRequestStream| {
        let sender = sender.clone();
        fasync::Task::local(
            stream
                .try_for_each(move |req| {
                    match req {
                        fapp::ViewProviderRequest::CreateView2 { args, .. } => {
                            let view_creation_token = args.view_creation_token.unwrap();
                            // We do not get passed a view ref so create our own.
                            let view_identity = fviews::ViewIdentityOnCreation::from(
                                ViewRefPair::new().expect("failed to create ViewRefPair"),
                            );
                            sender
                                .unbounded_send(InternalMessage::CreateView(
                                    view_creation_token,
                                    view_identity,
                                ))
                                .expect("failed to send InternalMessage.");
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
    sender: UnboundedSender<InternalMessage>,
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
                                .unbounded_send(InternalMessage::OnNextFrameBegin {
                                    additional_present_credits,
                                    future_presentation_infos,
                                })
                                .expect("failed to send InternalMessage");
                        } else {
                            // If not an error, all table fields are guaranteed to be present.
                            unreachable!()
                        }
                    }
                    fland::FlatlandEvent::OnFramePresented { frame_presented_info } => {
                        sender
                            .unbounded_send(InternalMessage::OnFramePresented {
                                frame_presented_info,
                            })
                            .expect("failed to send InternalMessage");
                    }
                    fland::FlatlandEvent::OnError { error } => {
                        sender
                            .unbounded_send(InternalMessage::OnPresentError { error })
                            .expect("failed to send InternalMessage.");
                    }
                };
                future::ok(())
            })
            .unwrap_or_else(|e| eprintln!("error listening for Flatland Events: {:?}", e)),
    )
    .detach();
}

#[fuchsia::main(logging_tags = ["flatland-view-provider-example"])]
async fn main() {
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    let args: Args = argh::from_env();
    let frame_usage = if args.use_vulkan { FrameUsage::Gpu } else { FrameUsage::Cpu };

    let (internal_sender, mut internal_receiver) = unbounded::<InternalMessage>();

    let flatland =
        connect_to_protocol::<fland::FlatlandMarker>().expect("error connecting to Flatland");
    flatland.set_debug_name("Flatland ViewProvider Example").expect("fidl error");

    let sched_lib = ThroughputScheduler::new();

    info!("Established Flatland connection");

    setup_fidl_services(internal_sender.clone());
    setup_handle_flatland_events(flatland.take_event_stream(), internal_sender.clone());

    let mut app = AppModel::new(&flatland, internal_sender.clone(), &sched_lib);
    let mut renderer = app.init_scene(frame_usage).await;

    let mut present_count = 0;
    loop {
        futures::select! {
          message = internal_receiver.next().fuse() => {
            if let Some(message) = message {
              match message {
                InternalMessage::CreateView(view_creation_token, view_identity) => {
                      app.create_parent_viewport_watcher(view_creation_token, view_identity);
                  }
                  InternalMessage::Relayout { width, height } => {
                      app.on_relayout(width, height);
                  }
                  InternalMessage::OnPresentError { error } => {
                      error!("OnPresentError({:?})", error);
                      break;
                  }
                  InternalMessage::OnNextFrameBegin {
                      additional_present_credits,
                      future_presentation_infos,
                  } => {
                    trace::duration!("gfx", "FlatlandViewProvider::OnNextFrameBegin");
                    let infos = future_presentation_infos
                    .iter()
                    .map(
                      |x| PresentationInfo{
                        latch_point: zx::Time::from_nanos(x.latch_point.unwrap()),
                        presentation_time: zx::Time::from_nanos(x.presentation_time.unwrap())
                      })
                    .collect();
                    sched_lib.on_next_frame_begin(additional_present_credits, infos);
                  }
                  InternalMessage::OnFramePresented { frame_presented_info } => {
                    trace::duration!("gfx", "FlatlandViewProvider::OnFramePresented");
                    let presented_infos = frame_presented_info.presentation_infos
                    .iter()
                    .map(|info| PresentedInfo{
                      present_received_time:
                        zx::Time::from_nanos(info.present_received_time.unwrap()),
                      actual_latch_point:
                        zx::Time::from_nanos(info.latched_time.unwrap()),
                    })
                    .collect();

                    sched_lib.on_frame_presented(
                      zx::Time::from_nanos(frame_presented_info.actual_presentation_time),
                      presented_infos);
                  }
                  InternalMessage::FocusChanged{ is_focused } => {
                    app.is_focused = is_focused;
                  }
                  InternalMessage::TouchEvent{timestamp, interaction: _, phase, position_in_viewport } => {
                    info!(
                        x = position_in_viewport[0], y = position_in_viewport[1], time = timestamp,
                        ?phase, "Received TouchEvent"
                    );
                  },
                  InternalMessage::MouseEvent{ timestamp, trace_flow_id: _, position_in_viewport,
                    scroll_v: _, scroll_h: _, pressed_buttons} => {
                    info!(
                        time = timestamp, x = position_in_viewport[0],
                        y = position_in_viewport[1], buttons = ?pressed_buttons,
                        "Received MouseEvent"
                    );
                  },
                }
            }
          }
          present_parameters = sched_lib.wait_to_update().fuse() => {
            trace::duration!("gfx", "FlatlandApp::PresentBegin");
            app.draw(present_parameters.expected_presentation_time, renderer.deref_mut());
            trace::flow_begin!("gfx", "Flatland::Present", present_count.into());
            present_count += 1;
            flatland
                .present(fland::PresentArgs {
                    requested_presentation_time: Some(present_parameters.requested_presentation_time.into_nanos()),
                    acquire_fences: None,
                    release_fences: None,
                    unsquashable: Some(present_parameters.unsquashable),
                    ..fland::PresentArgs::EMPTY
                })
                .unwrap_or(());
          }
        }
    }
}
