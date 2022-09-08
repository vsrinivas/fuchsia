// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
mod internal_message;
mod touch;

use {
    async_utils::hanging_get::client::HangingGetStream,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_element::{
        GraphicalPresenterMarker, GraphicalPresenterProxy, ViewControllerMarker,
        ViewControllerProxy, ViewSpec,
    },
    fidl_fuchsia_math as fmath, fidl_fuchsia_ui_composition as fland,
    fidl_fuchsia_ui_pointer as fptr,
    fidl_fuchsia_ui_pointer::EventPhase,
    fidl_fuchsia_ui_views as fviews, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_scenic::ViewRefPair,
    fuchsia_trace as trace,
    futures::{
        channel::mpsc::{unbounded, UnboundedSender},
        future,
        prelude::*,
    },
    internal_message::InternalMessage,
    tracing::{error, warn},
};

const IMAGE_ID: fland::ContentId = fland::ContentId { value: 2 };
const TRANSFORM_ID: fland::TransformId = fland::TransformId { value: 3 };

struct AppModel<'a> {
    flatland: &'a fland::FlatlandProxy,
    graphical_presenter: &'a GraphicalPresenterProxy,
    internal_sender: UnboundedSender<InternalMessage>,
    view_controller: Option<ViewControllerProxy>,
    color: fland::ColorRgba,
    size: fmath::SizeU,
}

impl<'a> AppModel<'a> {
    fn new(
        flatland: &'a fland::FlatlandProxy,
        graphical_presenter: &'a GraphicalPresenterProxy,
        internal_sender: UnboundedSender<InternalMessage>,
    ) -> AppModel<'a> {
        AppModel {
            flatland,
            graphical_presenter,
            internal_sender,
            view_controller: None,
            color: fland::ColorRgba { red: 1.0, green: 1.0, blue: 0.0, alpha: 1.0 },
            size: fmath::SizeU { width: 100, height: 100 },
        }
    }

    async fn init_scene(&mut self) {
        // Create a rectangle that will fill the whole screen.
        self.flatland.create_filled_rect(&mut IMAGE_ID.clone()).expect("fidl error");
        self.flatland
            .set_solid_fill(&mut IMAGE_ID.clone(), &mut self.color, &mut self.size)
            .expect("fidl error");

        // Populate the rest of the Flatland scene. There is a single transform which is set as the
        // root transform; the newly-created image is set as the content of that transform.
        self.flatland.create_transform(&mut TRANSFORM_ID.clone()).expect("fidl error");
        self.flatland.set_root_transform(&mut TRANSFORM_ID.clone()).expect("fidl error");
        self.flatland
            .set_content(&mut TRANSFORM_ID.clone(), &mut IMAGE_ID.clone())
            .expect("fidl error");
    }

    async fn create_view(&mut self) {
        // Set up the channel to listen for layout changes.
        let (parent_viewport_watcher, parent_viewport_watcher_request) =
            create_proxy::<fland::ParentViewportWatcherMarker>()
                .expect("failed to create ParentViewportWatcherProxy");

        // Set up the protocols we care about (currently just touch).
        let (touch, touch_request) =
            create_proxy::<fptr::TouchSourceMarker>().expect("failed to create TouchSource");
        let view_bound_protocols = fland::ViewBoundProtocols {
            touch_source: Some(touch_request),
            ..fland::ViewBoundProtocols::EMPTY
        };

        // Create the view.
        let fuchsia_scenic::flatland::ViewCreationTokenPair {
            mut view_creation_token,
            viewport_creation_token,
        } = fuchsia_scenic::flatland::ViewCreationTokenPair::new()
            .expect("failed to create view tokens");
        self.flatland
            .create_view2(
                &mut view_creation_token,
                &mut fviews::ViewIdentityOnCreation::from(
                    ViewRefPair::new().expect("failed viewref creation"),
                ),
                view_bound_protocols,
                parent_viewport_watcher_request,
            )
            .expect("fidl error");

        // Connect to graphical presenter to get the view displayed.
        let (view_controller_proxy, view_controller_request) =
            create_proxy::<ViewControllerMarker>().unwrap();
        self.view_controller = Some(view_controller_proxy);
        let view_spec =
            ViewSpec { viewport_creation_token: Some(viewport_creation_token), ..ViewSpec::EMPTY };
        self.graphical_presenter
            .present_view(view_spec, None, Some(view_controller_request))
            .await
            .expect("failed to present view")
            .unwrap_or_else(|e| println!("{:?}", e));

        // Listen for updates over channels we just created.
        Self::spawn_layout_info_watcher(parent_viewport_watcher, self.internal_sender.clone());
        touch::spawn_touch_source_watcher(touch, self.internal_sender.clone());
    }

    fn spawn_layout_info_watcher(
        parent_viewport_watcher: fland::ParentViewportWatcherProxy,
        sender: UnboundedSender<InternalMessage>,
    ) {
        fasync::Task::spawn(async move {
            let mut layout_info_stream = HangingGetStream::new(
                parent_viewport_watcher,
                fland::ParentViewportWatcherProxy::get_layout,
            );

            while let Some(result) = layout_info_stream.next().await {
                match result {
                    Ok(layout_info) => {
                        sender
                            .unbounded_send(InternalMessage::Relayout {
                                size: layout_info
                                    .logical_size
                                    .unwrap_or(fmath::SizeU { width: 0, height: 0 }),
                            })
                            .expect("failed to send InternalMessage.");
                    }
                    Err(fidl_error) => {
                        warn!("ParentViewportWatcher GetLayout() error: {:?}", fidl_error);
                        return;
                    }
                }
            }
        })
        .detach();
    }

    fn on_relayout(&mut self, size: fmath::SizeU) {
        self.size = size;
        self.flatland
            .set_solid_fill(&mut IMAGE_ID.clone(), &mut self.color, &mut self.size)
            .expect("fidl error");
    }

    fn next_color(&mut self) {
        self.color = fland::ColorRgba {
            red: (self.color.red + 0.0625) % 1.0,
            green: (self.color.green + 0.125) % 1.0,
            blue: (self.color.blue + 0.25) % 1.0,
            alpha: self.color.alpha,
        };
        self.flatland
            .set_solid_fill(&mut IMAGE_ID.clone(), &mut self.color, &mut self.size)
            .expect("fidl error");
    }
}

fn setup_handle_flatland_events(
    event_stream: fland::FlatlandEventStream,
    sender: UnboundedSender<InternalMessage>,
) {
    fasync::Task::local(
        event_stream
            .try_for_each(move |event| {
                match event {
                    fland::FlatlandEvent::OnNextFrameBegin { .. } => {
                        sender
                            .unbounded_send(InternalMessage::OnNextFrameBegin)
                            .expect("failed to send InternalMessage");
                    }
                    fland::FlatlandEvent::OnFramePresented { .. } => {}
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

#[fuchsia::main]
async fn main() {
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    let (internal_sender, mut internal_receiver) = unbounded::<InternalMessage>();

    let flatland =
        connect_to_protocol::<fland::FlatlandMarker>().expect("error connecting to Flatland");
    flatland.set_debug_name("Flatland ViewProvider Example").expect("fidl error");

    let graphical_presenter = connect_to_protocol::<GraphicalPresenterMarker>()
        .expect("error connecting to GraphicalPresenter");

    setup_handle_flatland_events(flatland.take_event_stream(), internal_sender.clone());

    let mut app = AppModel::new(&flatland, &graphical_presenter, internal_sender.clone());
    app.init_scene().await;
    app.create_view().await;

    // Call present once here and then continuously during the OnNextFrameBegin event.
    flatland.present(fland::PresentArgs::EMPTY).expect("Present call failed");
    let mut present_count = 1;

    // Vec for tracking touch event trace flow ids.
    let mut touch_updates = Vec::<trace::Id>::new();

    loop {
        futures::select! {
          message = internal_receiver.next().fuse() => {
            if let Some(message) = message {
              match message {
                  InternalMessage::Relayout { size } => {
                      app.on_relayout(size);
                  }
                  InternalMessage::OnPresentError { error } => {
                      error!("OnPresentError({:?})", error);
                      break;
                  }
                  InternalMessage::OnNextFrameBegin {} => {
                    trace::duration!("gfx", "SimplestApp::OnNextFrameBegin");

                    // End all flows from where the update was applied.
                    for trace_id in touch_updates.drain(..) {
                        trace::flow_end!("input", "touch_update", trace_id);
                    }

                    // Present all pending updates with a trace flow into Scenic based on
                    // present_count.
                    trace::flow_begin!("gfx", "Flatland::Present", present_count.into());
                    flatland.present(fland::PresentArgs::EMPTY).expect("Present call failed");
                    present_count += 1;
                  }
                  InternalMessage::TouchEvent{ phase, trace_id } => {
                    trace::duration!("input", "OnTouchEvent");
                    trace::flow_end!("input", "dispatch_event_to_client", trace_id);
                    // Change color on every finger down event.
                    if phase == EventPhase::Add {
                        // Trace from now until the update is applied.
                        let trace_id = trace::Id::new();
                        touch_updates.push(trace_id);
                        trace::flow_begin!("input", "touch_update", trace_id);
                        app.next_color();
                    }
                  },
                }
            }
          }
        }
    }
}
