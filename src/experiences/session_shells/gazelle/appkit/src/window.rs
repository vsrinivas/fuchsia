// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    collections::HashMap,
    sync::{Arc, Mutex},
};

use anyhow::{anyhow, Error};
use async_utils::hanging_get::client::HangingGetStream;
use fidl::{
    endpoints::{create_endpoints, create_proxy, create_request_stream, ClientEnd, ServerEnd},
    AsHandleRef,
};
use fidl_fuchsia_element as felement;
use fidl_fuchsia_math as fmath;
use fidl_fuchsia_ui_composition as ui_comp;
use fidl_fuchsia_ui_input3 as ui_input3;
use fidl_fuchsia_ui_pointer as fptr;
use fidl_fuchsia_ui_shortcut2 as ui_shortcut2;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_async as fasync;
use fuchsia_scenic::flatland::{IdGenerator, ViewCreationTokenPair};
use futures::{
    channel::mpsc::UnboundedSender, FutureExt, Stream, StreamExt, TryFutureExt, TryStreamExt,
};
use pointer_fusion::*;
use tracing::*;

use crate::{
    child_view::ChildView,
    event::{Event, EventSender, ViewSpecHolder, WindowEvent},
    image::{Image, ImageData},
    utils::{ProductionProtocolConnector, ProtocolConnector},
};

struct Presenter {
    flatland: ui_comp::FlatlandProxy,
    can_update: bool,
    needs_update: bool,
}

impl Presenter {
    pub fn new(flatland: ui_comp::FlatlandProxy) -> Self {
        Presenter { flatland, can_update: true, needs_update: false }
    }

    pub fn on_next_frame(
        &mut self,
        _next_frame_info: ui_comp::OnNextFrameBeginValues,
    ) -> Result<(), Error> {
        if self.needs_update {
            self.needs_update = false;
            self.can_update = false;
            self.present()
        } else {
            self.can_update = true;
            Ok(())
        }
    }

    pub fn redraw(&mut self) -> Result<(), Error> {
        if self.can_update {
            self.needs_update = false;
            self.can_update = false;
            self.present()
        } else {
            self.needs_update = true;
            Ok(())
        }
    }

    fn present(&self) -> Result<(), Error> {
        self.flatland.present(ui_comp::PresentArgs::EMPTY)?;
        Ok(())
    }
}

/// Defines a type to hold an id to the window. This implementation uses the value of
/// [ViewCreationToken] to be the window id.
#[derive(Debug, Copy, Clone, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct WindowId(pub u64);

impl WindowId {
    pub fn from_view_creation_token(token: &ui_views::ViewCreationToken) -> Self {
        Self(token.value.raw_handle().into())
    }
}

/// Defines a struct to hold window attributes used to create the window.
#[derive(Default)]
pub(crate) struct WindowAttributes {
    /// The title of the window. Only used when presented to the system's GraphicalPresenter.
    pub title: Option<String>,
    /// The [ViewCreationToken] passed to the application's [ViewProvider]. Unused for windows
    /// presented to the system's GraphicalPresenter.
    pub view_creation_token: Option<ui_views::ViewCreationToken>,
}

const ROOT_TRANSFORM_ID: ui_comp::TransformId = ui_comp::TransformId { value: 1 };

/// Defines a struct to hold [Window] state.
pub struct Window<T> {
    attributes: WindowAttributes,
    id: WindowId,
    id_generator: IdGenerator,
    flatland: Option<ui_comp::FlatlandProxy>,
    view_ref: ui_views::ViewRef,
    view_identity: ui_views::ViewIdentityOnCreation,
    annotations: Option<Vec<felement::Annotation>>,
    annotation_controller_request_stream: Option<felement::AnnotationControllerRequestStream>,
    view_controller_proxy: Option<felement::ViewControllerProxy>,
    focuser: Option<ui_views::FocuserProxy>,
    shortcut_task: Option<fasync::Task<()>>,
    event_sender: EventSender<T>,
    running_tasks: Vec<fasync::Task<()>>,
    presenter: Option<Arc<Mutex<Presenter>>>,
    protocol_connector: Box<dyn ProtocolConnector>,
}

impl<T> Window<T> {
    pub fn new(event_sender: EventSender<T>) -> Window<T> {
        let id_generator = IdGenerator::new_with_first_id(ROOT_TRANSFORM_ID.value);
        let id = WindowId(0);
        let attributes = WindowAttributes::default();
        let view_ref_pair =
            fuchsia_scenic::ViewRefPair::new().expect("Failed to create ViewRefPair");
        let view_ref = fuchsia_scenic::duplicate_view_ref(&view_ref_pair.view_ref)
            .expect("Failed to duplicate ViewRef");
        let view_identity = ui_views::ViewIdentityOnCreation::from(view_ref_pair);

        let protocol_connector = Box::new(ProductionProtocolConnector());

        Self {
            attributes,
            id,
            id_generator,
            flatland: None,
            view_ref,
            view_identity,
            annotations: None,
            annotation_controller_request_stream: None,
            view_controller_proxy: None,
            focuser: None,
            shortcut_task: None,
            event_sender,
            running_tasks: vec![],
            presenter: None,
            protocol_connector,
        }
    }

    pub fn with_title(mut self, title: String) -> Window<T> {
        self.attributes.title = Some(title);
        self
    }

    pub fn with_view_creation_token(mut self, token: ui_views::ViewCreationToken) -> Window<T> {
        self.attributes.view_creation_token = Some(token);
        self
    }

    pub fn with_protocol_connector(
        mut self,
        protocol_connector: Box<dyn ProtocolConnector>,
    ) -> Window<T> {
        self.protocol_connector = protocol_connector;
        self
    }

    pub fn id(&self) -> WindowId {
        self.id
    }

    pub fn get_flatland(&self) -> ui_comp::FlatlandProxy {
        self.flatland.clone().expect("Not connected to Flatland. Window may not be created yet")
    }

    pub fn get_root_transform_id(&self) -> ui_comp::TransformId {
        ROOT_TRANSFORM_ID.clone()
    }

    pub fn next_transform_id(&mut self) -> ui_comp::TransformId {
        self.id_generator.next_transform_id()
    }

    pub fn next_content_id(&mut self) -> ui_comp::ContentId {
        self.id_generator.next_content_id()
    }

    pub fn set_content(
        &self,
        mut transform_id: ui_comp::TransformId,
        mut content_id: ui_comp::ContentId,
    ) {
        self.get_flatland()
            .set_content(&mut transform_id, &mut content_id)
            .expect("Failed to set content");
    }

    pub fn close(&mut self) -> Result<(), Error> {
        // If we have a ViewController through GraphicalPresenter, notify it of window close.
        if let Some(view_controller_proxy) = self.view_controller_proxy.take() {
            view_controller_proxy.dismiss()?;
        }

        Ok(())
    }

    pub fn request_focus(&mut self, view_ref: ui_views::ViewRef) {
        if let Some(focuser) = self.focuser.clone() {
            let mut dup_view_ref = fuchsia_scenic::duplicate_view_ref(&view_ref)
                .expect("Failed to duplicate view_ref for request_focus");
            let task = fasync::Task::spawn(async move {
                if let Err(error) = focuser.request_focus(&mut dup_view_ref).await {
                    error!("Failed to request focus on a view: {:?}", error);
                }
            });
            self.running_tasks.push(task);
        }
    }

    pub fn register_shortcuts(&mut self, shortcuts: Vec<ui_shortcut2::Shortcut>)
    where
        T: 'static + Sync + Send,
    {
        let mut view_ref_for_shortcuts = fuchsia_scenic::duplicate_view_ref(&self.view_ref)
            .expect("Failed to duplicate ViewRef");
        let window_id = self.id();
        let event_sender = self.event_sender.clone();

        let registry = self
            .protocol_connector
            .connect_to_shortcuts_registry()
            .expect("failed to connect to fuchsia.ui.shortcut2.Registry");

        let task = fasync::Task::spawn(async move {
            let (listener_client_end, mut listener_stream) =
                create_request_stream::<ui_shortcut2::ListenerMarker>()
                    .expect("Failed to create shortcut listener stream");

            if let Err(error) = registry.set_view(&mut view_ref_for_shortcuts, listener_client_end)
            {
                error!("Failed to set_view on fuchsia.ui.shortcut2.Registry: {:?}", error);
                return;
            }

            for shortcut in &shortcuts {
                if let Err(error) = registry.register_shortcut(&mut shortcut.clone()).await {
                    error!("Encountered error {:?} registering shortcut: {:?}", error, shortcut);
                }
            }

            while let Some(request) = listener_stream.next().await {
                match request {
                    Ok(ui_shortcut2::ListenerRequest::OnShortcut { id, responder }) => {
                        event_sender
                            .send(Event::WindowEvent {
                                window_id,
                                event: WindowEvent::Shortcut { id, responder },
                            })
                            .expect("Failed to send WindowEvent::Shortcut event");
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
                        error!("Shortcut listener connection closed.");
                        break;
                    }
                    Err(fidl_error) => {
                        error!("Shortcut listener error: {:?}", fidl_error);
                        break;
                    }
                }
            }
        });

        // Hold a reference to `fasync::Task` to keep the shortcut listener alive. First cancel
        // previous task.
        if let Some(task) = self.shortcut_task.take() {
            let _ = task.cancel();
        }
        self.shortcut_task = Some(task);
    }

    pub fn redraw(&mut self) {
        let _lock = self
            .presenter
            .as_ref()
            .expect("Not connected to Flatland. Window may not be created yet")
            .try_lock()
            .map(|mut presenter| presenter.redraw())
            .expect("Failed to lock presenter");
    }

    pub fn create_view(&mut self) -> Result<(), Error>
    where
        T: 'static + Sync + Send,
    {
        // `create_view` can be called only once!
        if self.id != WindowId(0) {
            return Err(anyhow!("create_view already called!"));
        }

        let flatland = self
            .protocol_connector
            .connect_to_flatland()
            .expect("Failed to connect to fuchsia.ui.comp.Flatland");
        flatland
            .create_transform(&mut ROOT_TRANSFORM_ID.clone())
            .expect("Failed to create transform");
        flatland
            .set_root_transform(&mut ROOT_TRANSFORM_ID.clone())
            .expect("Failed to set root transform");

        let presenter = Arc::new(Mutex::new(Presenter::new(flatland.clone())));

        let (mut view_creation_token, viewport_creation_token) =
            // Check if view_creation_token was passed from ViewProvider.
            match self.attributes.view_creation_token.take() {
                Some(view_creation_token) => (view_creation_token, None),
                None => {
                    // Create a pair of view creation token to present to GraphicalPresenter.
                    let ViewCreationTokenPair { view_creation_token, viewport_creation_token } =
                        ViewCreationTokenPair::new()?;
                    (view_creation_token, Some(viewport_creation_token))
                }
            };
        let (parent_viewport_watcher, parent_viewport_watcher_request) =
            create_proxy::<ui_comp::ParentViewportWatcherMarker>()?;
        let (focused, focused_request) = create_proxy::<ui_views::ViewRefFocusedMarker>()?;
        let (view_focuser, view_focuser_request) = create_proxy::<ui_views::FocuserMarker>()?;
        let (mouse, mouse_request) = create_proxy::<fptr::MouseSourceMarker>()?;
        let (touch, touch_request) = create_proxy::<fptr::TouchSourceMarker>()?;

        self.id = WindowId::from_view_creation_token(&view_creation_token);
        self.focuser = Some(view_focuser);

        let view_bound_protocols = ui_comp::ViewBoundProtocols {
            view_ref_focused: Some(focused_request),
            view_focuser: Some(view_focuser_request),
            mouse_source: Some(mouse_request),
            touch_source: Some(touch_request),
            ..ui_comp::ViewBoundProtocols::EMPTY
        };

        self.flatland = Some(flatland);
        self.presenter = Some(presenter);

        self.get_flatland().create_view2(
            &mut view_creation_token,
            &mut self.view_identity,
            view_bound_protocols,
            parent_viewport_watcher_request,
        )?;
        self.redraw();

        let window_id = self.id();
        let event_sender = self.event_sender.clone();

        let flatland_events_fut = serve_flatland_events(
            window_id,
            self.get_flatland(),
            self.presenter.clone().expect("Failed to clone Presenter"),
            event_sender.clone(),
        );

        let layout_watcher_fut = serve_layout_info_watcher(
            window_id,
            parent_viewport_watcher.clone(),
            event_sender.clone(),
        );

        let viewref_focused_fut =
            serve_view_ref_focused_watcher(window_id, focused, event_sender.clone());

        // If we created a viewport_creation_token earlier, we intend to present to the system's
        // GraphicalPresenter. Connect to it to present the window.
        let (graphical_presenter_fut, view_controller_fut) = match viewport_creation_token {
            Some(viewport_creation_token) => {
                let (annotation_controller_client_end, annotation_controller_server_end) =
                    create_endpoints::<felement::AnnotationControllerMarker>()?;
                let (view_controller_proxy, view_controller_request) =
                    create_proxy::<felement::ViewControllerMarker>()?;
                let view_ref_for_graphical_presenter =
                    fuchsia_scenic::duplicate_view_ref(&self.view_ref)?;
                let graphical_presenter = self
                    .protocol_connector
                    .connect_to_graphical_presenter()
                    .expect("Failed to connect to GraphicalPresenter");

                self.annotation_controller_request_stream =
                    Some(annotation_controller_server_end.into_stream().expect("FIDL error"));
                self.view_controller_proxy = Some(view_controller_proxy.clone());
                self.annotations = annotations_from_window_attributes(&self.attributes);
                (
                    present_to_graphical_presenter(
                        graphical_presenter,
                        self.annotations.take(),
                        viewport_creation_token,
                        view_ref_for_graphical_presenter,
                        annotation_controller_client_end,
                        view_controller_request,
                    )
                    .boxed(),
                    wait_for_view_controller_close(
                        window_id,
                        view_controller_proxy,
                        event_sender.clone(),
                    )
                    .boxed(),
                )
            }
            None => (async {}.boxed(), async {}.boxed()),
        };

        let view_ref_for_keyboard = fuchsia_scenic::duplicate_view_ref(&self.view_ref)?;
        let keyboard =
            self.protocol_connector.connect_to_keyboard().expect("Failed to connect to Keyboard");

        let keyboard_fut = serve_keyboard_listener(
            window_id,
            view_ref_for_keyboard,
            keyboard,
            event_sender.clone(),
        )
        .boxed();

        // Collect all futures into an abortable spawned task. The task is aborted in [Drop].
        let task = fasync::Task::spawn(async move {
            // Connect to GraphicalPresenter to present our view.
            graphical_presenter_fut.await;

            // Wait for first layout information.
            let result = parent_viewport_watcher.get_layout().await;

            let pixel_ratio = match result {
                Ok(layout_info) => {
                    let (width, height, pixel_ratio) = dimensions_from_layout_info(layout_info);
                    event_sender
                        .send(Event::WindowEvent {
                            window_id,
                            event: WindowEvent::Resized { width, height, pixel_ratio },
                        })
                        .expect("Failed to send WindowEvent::Resized event");
                    pixel_ratio
                }
                Err(fidl::Error::ClientChannelClosed { .. }) => {
                    warn!("ParentViewportWatcher connection closed.");
                    event_sender
                        .send(Event::WindowEvent { window_id, event: WindowEvent::Closed })
                        .expect("Failed to send WindowEvent::Closed event");
                    return;
                }
                Err(fidl_error) => {
                    warn!("ParentViewportWatcher GetLayout() error: {:?}", fidl_error);
                    return;
                }
            };

            // For pointer fusion, we need the device_pixel_ratio to convert pointer coordinates
            // from logical to physical coordinates.
            let (input_sender, pointer_stream) = pointer_fusion(pixel_ratio);

            let pointer_fut = serve_pointer_events(window_id, pointer_stream, event_sender.clone());

            let mouse_fut = serve_mouse_source_watcher(mouse, input_sender.clone());

            let touch_fut = serve_touch_source_watcher(touch, input_sender.clone());

            futures::join!(
                flatland_events_fut,
                layout_watcher_fut,
                viewref_focused_fut,
                view_controller_fut,
                keyboard_fut,
                pointer_fut,
                mouse_fut,
                touch_fut,
            );
        });
        self.running_tasks.push(task);

        Ok(())
    }

    /// Creates an instance of [ChildView] given a [ViewSpecHolder].
    pub fn create_child_view(
        &mut self,
        view_spec_holder: ViewSpecHolder,
        width: u32,
        height: u32,
        event_sender: EventSender<T>,
    ) -> Result<ChildView<T>, Error>
    where
        T: 'static + Sync + Send,
    {
        let viewport_content_id = self.next_content_id();
        let child_view = ChildView::new(
            self.get_flatland(),
            self.id,
            viewport_content_id,
            view_spec_holder,
            width,
            height,
            event_sender,
        )?;
        Ok(child_view)
    }

    /// Creates an instance of [ChildView] given a [ViewportCreationToken].
    pub fn create_child_view_from_viewport(
        &mut self,
        viewport_creation_token: ui_views::ViewportCreationToken,
        width: u32,
        height: u32,
        event_sender: EventSender<T>,
    ) -> Result<ChildView<T>, Error>
    where
        T: 'static + Sync + Send,
    {
        self.create_child_view(
            ViewSpecHolder {
                view_spec: felement::ViewSpec {
                    viewport_creation_token: Some(viewport_creation_token),
                    ..felement::ViewSpec::EMPTY
                },
                annotation_controller: None,
                view_controller_request: None,
                responder: None,
            },
            width,
            height,
            event_sender,
        )
    }

    /// Creates an instance of flatland [Image] given width, height and image data in
    /// [ui_comp::BufferCollectionImportToken] token at [vmo_index].
    pub fn create_image(&mut self, image_data: &mut ImageData) -> Result<Image, Error> {
        let content_id = self.next_content_id();
        Image::new(image_data, self.get_flatland(), content_id)
    }
}

async fn serve_flatland_events<T>(
    window_id: WindowId,
    flatland: ui_comp::FlatlandProxy,
    presenter: Arc<Mutex<Presenter>>,
    event_sender: EventSender<T>,
) {
    let flatland_event_stream = flatland.take_event_stream();
    let event_sender = &event_sender;
    flatland_event_stream
        .try_for_each(move |event| {
            match event {
                ui_comp::FlatlandEvent::OnNextFrameBegin { values } => {
                    let next_present_time = values
                        .future_presentation_infos
                        .as_ref()
                        .and_then(|infos| infos.first())
                        .and_then(|info| info.presentation_time)
                        .unwrap_or(0i64);
                    let _lock = presenter
                        .try_lock()
                        .map(|mut presenter| presenter.on_next_frame(values))
                        .expect("Failed to call on_next_frame on presenter");
                    event_sender
                        .send(Event::WindowEvent {
                            window_id,
                            event: WindowEvent::NeedsRedraw { next_present_time },
                        })
                        .expect("Failed to send WindowEvent::NeedsRedraw event");
                }
                ui_comp::FlatlandEvent::OnFramePresented { .. } => {}
                ui_comp::FlatlandEvent::OnError { error } => {
                    error!("Flatland error: {:?}", error);
                }
            };
            futures::future::ok(())
        })
        .unwrap_or_else(|e| error!("error listening for Flatland Events: {:?}", e))
        .await;
}

async fn serve_layout_info_watcher<T>(
    window_id: WindowId,
    parent_viewport_watcher: ui_comp::ParentViewportWatcherProxy,
    event_sender: EventSender<T>,
) {
    let mut layout_info_stream = HangingGetStream::new(
        parent_viewport_watcher,
        ui_comp::ParentViewportWatcherProxy::get_layout,
    );

    while let Some(result) = layout_info_stream.next().await {
        match result {
            Ok(layout_info) => {
                let (width, height, pixel_ratio) = dimensions_from_layout_info(layout_info);

                event_sender
                    .send(Event::WindowEvent {
                        window_id,
                        event: WindowEvent::Resized { width, height, pixel_ratio },
                    })
                    .expect("Failed to send WindowEvent::Resized event");
            }
            Err(fidl::Error::ClientChannelClosed { .. }) => {
                info!("ParentViewportWatcher connection closed.");
                event_sender
                    .send(Event::WindowEvent { window_id, event: WindowEvent::Closed })
                    .expect("Failed to send WindowEvent::Closed event");
                break;
            }
            Err(fidl_error) => {
                warn!("ParentViewportWatcher GetLayout() error: {:?}", fidl_error);
            }
        }
    }
}

async fn serve_view_ref_focused_watcher<T>(
    window_id: WindowId,
    focused: ui_views::ViewRefFocusedProxy,
    event_sender: EventSender<T>,
) {
    let mut focused_stream = HangingGetStream::new(focused, ui_views::ViewRefFocusedProxy::watch);
    while let Some(result) = focused_stream.next().await {
        match result {
            Ok(ui_views::FocusState { focused: Some(focused), .. }) => {
                event_sender
                    .send(Event::WindowEvent { window_id, event: WindowEvent::Focused { focused } })
                    .expect("Failed to send WindowEvent::Focused event");
            }
            Ok(ui_views::FocusState { focused: None, .. }) => {
                error!("Missing required field FocusState.focused");
            }
            Err(fidl::Error::ClientChannelClosed { .. }) => {
                error!("ViewRefFocused connection closed.");
                break;
            }
            Err(fidl_error) => {
                error!("ViewRefFocused fidl error: {:?}", fidl_error);
                break;
            }
        }
    }
}

async fn serve_mouse_source_watcher(
    mouse_source: fptr::MouseSourceProxy,
    input_sender: UnboundedSender<InputEvent>,
) {
    let mut mouse_source_stream =
        HangingGetStream::new(mouse_source, fptr::MouseSourceProxy::watch);

    while let Some(result) = mouse_source_stream.next().await {
        match result {
            Ok(events) => {
                for event in events.iter() {
                    input_sender
                        .unbounded_send(InputEvent::MouseEvent(event.clone()))
                        .expect("Failed to send InputEvent::MouseEvent");
                }
            }
            Err(fidl::Error::ClientChannelClosed { .. }) => {
                error!("MouseSource connection closed.");
                break;
            }
            Err(fidl_error) => {
                warn!("MouseSource Watch() error: {:?}", fidl_error);
                break;
            }
        }
    }
}

async fn serve_touch_source_watcher(
    touch_source: fptr::TouchSourceProxy,
    input_sender: UnboundedSender<InputEvent>,
) {
    // Holds the responses to all previously received events.
    let mut pending_responses: Vec<fptr::TouchResponse> = vec![];

    // Buffers touch events until their `fptr::Interaction` status is granted.
    let mut interactions: HashMap<fptr::TouchInteractionId, Vec<fptr::TouchEvent>> = HashMap::new();

    loop {
        let events = touch_source.watch(&mut pending_responses.into_iter());
        match events.await {
            Ok(events) => {
                // Generate responses which will be sent with the next call to `watch`.
                pending_responses = events
                    .iter()
                    .map(|event| {
                        let mut response = fptr::TouchResponse::EMPTY;
                        if let Some(fptr::TouchPointerSample {
                            interaction: Some(interaction),
                            phase: Some(phase),
                            position_in_viewport: Some(_),
                            ..
                        }) = event.pointer_sample
                        {
                            if phase == fptr::EventPhase::Add && event.interaction_result.is_none()
                            {
                                interactions.insert(interaction, vec![]);
                            }

                            if let Some(interaction) = interactions.get_mut(&interaction) {
                                interaction.push(event.clone());
                            } else {
                                input_sender
                                    .unbounded_send(InputEvent::TouchEvent(event.clone()))
                                    .expect("Failed to send InputEvent::TouchEvent");
                            }
                            response = fptr::TouchResponse {
                                response_type: Some(fptr::TouchResponseType::Yes),
                                trace_flow_id: event.trace_flow_id,
                                ..response
                            }
                        }

                        if let Some(result) = event.interaction_result {
                            let interaction = result.interaction;
                            if result.status == fptr::TouchInteractionStatus::Granted
                                && interactions.contains_key(&interaction)
                            {
                                if let Some(events) = interactions.remove(&interaction) {
                                    for event in events {
                                        input_sender
                                            .unbounded_send(InputEvent::TouchEvent(event.clone()))
                                            .expect("Failed to send InputEvent::TouchEvent");
                                    }
                                }
                            }
                        }

                        response
                    })
                    .collect();
            }
            Err(fidl::Error::ClientChannelClosed { .. }) => {
                error!("TouchSource connection closed.");
                break;
            }
            Err(fidl_error) => {
                warn!("TouchSource Watch() error: {:?}", fidl_error);
                break;
            }
        }
    }
}

async fn serve_pointer_events<T>(
    window_id: WindowId,
    mut stream: impl Stream<Item = PointerEvent> + std::marker::Unpin,
    event_sender: EventSender<T>,
) {
    while let Some(event) = stream.next().await {
        event_sender
            .send(Event::WindowEvent { window_id, event: WindowEvent::Pointer { event } })
            .expect("failed to send Message.");
    }
}

async fn present_to_graphical_presenter(
    graphical_presenter: felement::GraphicalPresenterProxy,
    annotations: Option<Vec<felement::Annotation>>,
    viewport_creation_token: ui_views::ViewportCreationToken,
    view_ref: ui_views::ViewRef,
    annotation_controller_client_end: ClientEnd<felement::AnnotationControllerMarker>,
    view_controller_request_stream: ServerEnd<felement::ViewControllerMarker>,
) {
    // TODO(https://fxbug.dev/107983): Remove view_ref once Ermine is updated to not need it.
    let view_spec = felement::ViewSpec {
        viewport_creation_token: Some(viewport_creation_token),
        view_ref: Some(view_ref),
        annotations: annotations,
        ..felement::ViewSpec::EMPTY
    };
    let _result = graphical_presenter
        .present_view(
            view_spec,
            Some(annotation_controller_client_end),
            Some(view_controller_request_stream),
        )
        .await
        .map_err(|e| error!("{:?}", e))
        .expect("Failed to present view to GraphicalPresenter");
}

async fn wait_for_view_controller_close<T>(
    window_id: WindowId,
    view_controller_proxy: felement::ViewControllerProxy,
    event_sender: EventSender<T>,
) {
    // Waits for view_controller_proxy.on_closed().
    let stream = view_controller_proxy.take_event_stream();
    let _ = stream.collect::<Vec<_>>().await;
    event_sender
        .send(Event::WindowEvent { window_id, event: WindowEvent::Closed })
        .expect("Failed to send WindowEvent::Closed event");
}

async fn serve_keyboard_listener<T>(
    window_id: WindowId,
    mut view_ref: ui_views::ViewRef,
    keyboard: ui_input3::KeyboardProxy,
    event_sender: EventSender<T>,
) {
    let (listener_client_end, mut listener_stream) =
        create_request_stream::<ui_input3::KeyboardListenerMarker>()
            .expect("failed to create listener stream");

    match keyboard.add_listener(&mut view_ref, listener_client_end).await {
        Ok(()) => {
            while let Some(Ok(event)) = listener_stream.next().await {
                let ui_input3::KeyboardListenerRequest::OnKeyEvent { event, responder, .. } = event;
                event_sender
                    .send(Event::WindowEvent {
                        window_id,
                        event: WindowEvent::Keyboard { event, responder },
                    })
                    .expect("Failed to send WindowEvent::Keyboard event");
            }
        }
        Err(e) => {
            error!("Failed to add listener to the keyboard: {:?}", e)
        }
    }
}

fn annotations_from_window_attributes(
    attributes: &WindowAttributes,
) -> Option<Vec<felement::Annotation>> {
    // TODO(https://fxbug.dev/108345): Stop hardcoding namespace for ermine shell.
    let title = attributes.title.clone()?;
    let annotations = vec![felement::Annotation {
        key: felement::AnnotationKey { namespace: "ermine".to_owned(), value: "name".to_owned() },
        value: felement::AnnotationValue::Text(title),
    }];
    Some(annotations)
}

fn dimensions_from_layout_info(layout_info: ui_comp::LayoutInfo) -> (u32, u32, f32) {
    let fmath::SizeU { width, height } =
        layout_info.logical_size.unwrap_or(fmath::SizeU { width: 0, height: 0 });

    let pixel_ratio = layout_info.device_pixel_ratio.unwrap_or(fmath::VecF { x: 1.0, y: 1.0 }).x;

    (width, height, pixel_ratio)
}
