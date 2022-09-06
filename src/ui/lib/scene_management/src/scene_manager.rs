// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::pointerinjector_config::{
        InjectorViewportChangeFn, InjectorViewportHangingGet, InjectorViewportSpec,
        InjectorViewportSubscriber,
    },
    crate::DisplayMetrics,
    anyhow::Error,
    async_trait::async_trait,
    async_utils::hanging_get::server as hanging_get,
    fidl_fuchsia_ui_app as ui_app, fidl_fuchsia_ui_composition as ui_comp,
    fidl_fuchsia_ui_pointerinjector_configuration::{
        SetupRequest as PointerInjectorConfigurationSetupRequest,
        SetupRequestStream as PointerInjectorConfigurationSetupRequestStream,
    },
    fidl_fuchsia_ui_scenic as ui_scenic, fidl_fuchsia_ui_views as ui_views,
    flatland_frame_scheduling_lib::*,
    fuchsia_async as fasync, fuchsia_scenic as scenic, fuchsia_scenic,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_trace as trace, fuchsia_zircon as zx,
    futures::channel::mpsc::{UnboundedReceiver, UnboundedSender},
    futures::channel::oneshot,
    futures::future::TryFutureExt,
    futures::prelude::*,
    parking_lot::Mutex,
    std::collections::VecDeque,
    std::sync::{Arc, Weak},
};

/// ViewHolder / Viewport token union.
pub enum ViewportToken {
    Gfx(ui_views::ViewHolderToken),
    Flatland(ui_views::ViewportCreationToken),
}

/// Presentation messages.
pub enum PresentationMessage {
    /// Request a present call.
    // TODO(fxbug.dev/86837): delete this message type when Gfx is removed.
    RequestPresent,
    // Requests a present call; also, provides a channel that will get a ping back
    // when the next frame has been presented on screen.
    //
    // TODO(fxbug.dev/86837): delete this message type when Gfx is removed.
    RequestPresentWithPingback(oneshot::Sender<()>),
    /// Submit a present call.
    Present,
}

/// Unbounded sender used for presentation messages.
pub type PresentationSender = UnboundedSender<PresentationMessage>;

/// Unbounded receiver used for presentation messages.
pub type PresentationReceiver = UnboundedReceiver<PresentationMessage>;

/// A [SceneManager] manages a Scenic scene graph, and allows clients to add views to it.
/// Each [`SceneManager`] can choose how to configure the scene, including lighting, setting the
/// frames of added views, etc.
///
/// # Example
///
/// ```
/// let view_provider = some_apips.connect_to_service::<ViewProviderMarker>()?;
///
/// let scenic = connect_to_service::<ScenicMarker>()?;
/// let mut scene_manager = scene_management::FlatSceneManager::new(scenic).await?;
/// scene_manager.set_root_view(viewport_token).await?;
///
/// ```
#[async_trait]
pub trait SceneManager: Send {
    /// Sets the root view for the scene.
    ///
    /// ViewRef will be unset for Flatland views.
    ///
    /// Removes any previous root view, as well as all of its descendants.
    async fn set_root_view(
        &mut self,
        viewport_token: ViewportToken,
        view_ref: Option<ui_views::ViewRef>,
    ) -> Result<(), Error>;

    /// DEPRECATED: Use ViewportToken version above.
    /// Sets the root view for the scene.
    ///
    /// Removes any previous root view, as well as all of its descendants.
    async fn set_root_view_deprecated(
        &mut self,
        view_provider: ui_app::ViewProviderProxy,
    ) -> Result<ui_views::ViewRef, Error>;

    /// Requests focus be transferred to the scene.
    fn request_focus(
        &self,
        view_ref: &mut ui_views::ViewRef,
    ) -> fidl::client::QueryResponseFut<ui_views::FocuserRequestFocusResult>;

    /// Inserts an a11y view into the scene.
    ///
    /// Removes the existing proxy view/viewholder pair, adds the a11y view/viewholder under the
    /// root, and creates a new proxy view. Then, attaches any existing views to the new proxy
    /// view.
    ///
    /// # Parameters
    /// - `a11y_view_holder_token`: The [`ViewHolderToken`] used to create the a11y view holder.
    ///
    /// # Returns
    /// The [`ViewHolderToken`] used to create the new a11y proxy view holder.
    ///
    /// # Errors
    /// Returns an error if the a11y view holder or proxy view could not be created or added to the
    /// scene.
    async fn insert_a11y_view(
        &mut self,
        a11y_view_holder_token: ui_views::ViewHolderToken,
    ) -> Result<ui_views::ViewHolderToken, Error>;

    fn insert_a11y_view2(
        &mut self,
        a11y_viewport_creation_token: ui_views::ViewportCreationToken,
    ) -> Result<ui_views::ViewportCreationToken, Error>;

    /// Sets the transform for screen magnification, applied after the camera projection.
    ///
    /// # Notes
    /// In gfx, this won't return until the next frame has rendered.
    /// Not implemented in flatland.
    async fn set_camera_clip_space_transform(&mut self, x: f32, y: f32, scale: f32);

    /// Resets the transform for screen magnification to the default.
    async fn reset_camera_clip_space_transform(&mut self);

    /// Sets the position of the cursor in the current scene. If no cursor has been created it will
    /// create one using default settings.
    ///
    /// # Parameters
    /// - `position`: A [`Position`] struct representing the cursor position.
    ///
    /// # Notes
    /// If a custom cursor has not been set using `set_cursor_image` or `set_cursor_shape` a default
    /// cursor will be created and added to the scene.  The implementation of the `SceneManager` trait
    /// is responsible for translating the raw input position into "pips".
    fn set_cursor_position(&mut self, position: input_pipeline::Position);

    /// Sets the visibility of the cursor in the current scene. The cursor is visible by default.
    ///
    /// # Parameters
    /// - `visible`: Boolean value indicating if the cursor should be visible.
    fn set_cursor_visibility(&mut self, visible: bool);

    // Supports the implementation of fuchsia.ui.pointerinjector.configurator.Setup.GetViewRefs()
    fn get_pointerinjection_view_refs(&self) -> (ui_views::ViewRef, ui_views::ViewRef);

    /// Input pipeline handlers such as TouchInjectorHandler require the display size in order to be
    /// instantiated.  This method exposes that information.
    fn get_pointerinjection_display_size(&self) -> input_pipeline::Size;

    // Support the hanging get implementation of
    // fuchsia.ui.pointerinjector.configurator.Setup.WatchViewport().
    fn get_pointerinjector_viewport_watcher_subscription(&self) -> InjectorViewportSubscriber;

    fn get_display_metrics(&self) -> &DisplayMetrics;
}

/// Listens for presentation requests and schedules presents.
/// Connects to the Scenic event stream to listen for OnFramePresented messages and calls present
/// when Scenic is ready for an update.
pub fn start_presentation_loop(
    sender: PresentationSender,
    mut receiver: PresentationReceiver,
    weak_session: Weak<Mutex<scenic::Session>>,
) {
    fasync::Task::local(async move {
        let mut event_stream = {
            let session = weak_session.upgrade().expect("Failed to acquire session");
            let event_stream = session.lock().take_event_stream();
            event_stream
        };
        let mut present_requested = false;
        let mut channels_awaiting_pingback: Vec<oneshot::Sender<()>> = Vec::new();

        while let Some(message) = receiver.next().await {
            match message {
                PresentationMessage::RequestPresent => {
                    // Queue a present if not already queued.
                    if !present_requested {
                        sender
                            .unbounded_send(PresentationMessage::Present)
                            .expect("failed to send Present message");
                        present_requested = true;
                    }
                }
                PresentationMessage::RequestPresentWithPingback(channel) => {
                    channels_awaiting_pingback.push(channel);
                    // Queue a present if not already queued.
                    if !present_requested {
                        sender
                            .unbounded_send(PresentationMessage::Present)
                            .expect("failed to send Present message");
                        present_requested = true;
                    }
                }
                PresentationMessage::Present => {
                    present_requested = false;
                    if let Some(session) = weak_session.upgrade() {
                        present(&session);

                        // Wait for frame to be presented before we queue another present.
                        // We structure this as a loop, despite clippy::never_loop, because it seems
                        // a good way to ensure that we necessarily revisit this code if another
                        // event is ever added in addition to `OnFramePresented`.
                        #[allow(clippy::never_loop)]
                        while let Some(event) =
                            event_stream.try_next().await.expect("Failed to get next event")
                        {
                            match event {
                                ui_scenic::SessionEvent::OnFramePresented {
                                    frame_presented_info: _,
                                } => break,
                            }
                        }

                        for channel in channels_awaiting_pingback.drain(0..) {
                            _ = channel.send(());
                        }
                    } else {
                        break;
                    }
                }
            }
        }
    })
    .detach();
}

/// Inform Scenic that is should render any pending changes
fn present(session: &scenic::SessionPtr) {
    fasync::Task::local(
        session
            .lock()
            // Passing 0 for requested_presentation_time tells scenic that that it should process
            // enqueued operation as soon as possible.
            // Passing 0 for requested_prediction_span guarantees that scenic will provide at least
            // one future time.
            .present2(0, 0)
            .map_ok(|_| ())
            .unwrap_or_else(|error| fx_log_err!("Present error: {:?}", error)),
    )
    .detach();
}

pub fn create_viewport_hanging_get(
    initial_spec: InjectorViewportSpec,
) -> Arc<Mutex<InjectorViewportHangingGet>> {
    let notify_fn: InjectorViewportChangeFn = Box::new(|viewport_spec, responder| {
        if let Err(fidl_error) = responder.send((*viewport_spec).into()) {
            fx_log_info!("Viewport hanging get notification, FIDL error: {}", fidl_error);
        }
        // TODO(fxbug.dev/87670): the HangingGet docs don't explain what value to return.
        true
    });

    Arc::new(Mutex::new(hanging_get::HangingGet::new(initial_spec, notify_fn)))
}

pub fn start_flatland_presentation_loop(
    mut receiver: PresentationReceiver,
    weak_flatland: Weak<Mutex<ui_comp::FlatlandProxy>>,
    debug_name: String,
) {
    fasync::Task::local(async move {
        let mut present_count = 0;
        let scheduler = ThroughputScheduler::new();
        let mut flatland_event_stream = {
            if let Some(flatland) = weak_flatland.upgrade() {
                flatland.lock().take_event_stream()
            } else {
                fx_log_warn!(
                    "Failed to upgrade Flatand weak ref; exiting presentation loop for {debug_name}"
                );
                return;
            }
        };

        let mut channels_awaiting_pingback = VecDeque::from([Vec::new()]);

        loop {
            futures::select! {
                message = receiver.next().fuse() => {
                    match message {
                        Some(PresentationMessage::RequestPresent) => {
                            scheduler.request_present();
                        }
                        Some(PresentationMessage::RequestPresentWithPingback(channel)) => {
                            channels_awaiting_pingback.back_mut().unwrap().push(channel);
                            scheduler.request_present();
                        }
                        Some(PresentationMessage::Present) => {
                            // TODO(fxbug.dev/108140): delete this message type when Gfx is removed.
                            panic!("PresentationMessage::Present is not allowed for {debug_name}");
                        }
                        None => {}
                    }
                }
                flatland_event = flatland_event_stream.next().fuse() => {
                    match flatland_event {
                        Some(Ok(ui_comp::FlatlandEvent::OnNextFrameBegin{ values })) => {
                            trace::duration!("scene_manager", "SceneManager::OnNextFrameBegin",
                                             "debug_name" => &*debug_name);
                            let credits = values
                                          .additional_present_credits
                                          .expect("Present credits must exist");
                            let infos = values
                                .future_presentation_infos
                                .expect("Future presentation infos must exist")
                                .iter()
                                .map(
                                |x| PresentationInfo{
                                    latch_point: zx::Time::from_nanos(x.latch_point.unwrap()),
                                    presentation_time: zx::Time::from_nanos(
                                                        x.presentation_time.unwrap())
                                })
                                .collect();
                            scheduler.on_next_frame_begin(credits, infos);
                        }
                        Some(Ok(ui_comp::FlatlandEvent::OnFramePresented{ frame_presented_info })) => {
                            trace::duration!("scene_manager", "SceneManager::OnFramePresented",
                                             "debug_name" => &*debug_name);
                            let actual_presentation_time =
                                zx::Time::from_nanos(frame_presented_info.actual_presentation_time);
                            let presented_infos: Vec<PresentedInfo> =
                                frame_presented_info.presentation_infos
                                .into_iter()
                                .map(|x| x.into())
                                .collect();

                            // Pingbacks for presented updates. For each presented frame, drain all
                            // of the corresponding pingback channels
                            for _ in 0..presented_infos.len() {
                                for channel in channels_awaiting_pingback.pop_back().unwrap() {
                                    _ = channel.send(());
                                }
                            }

                            scheduler.on_frame_presented(actual_presentation_time, presented_infos);
                        }
                        Some(Ok(ui_comp::FlatlandEvent::OnError{ error })) => {
                            fx_log_err!(
                                "Received FlatlandError code: {}; exiting listener loop for {debug_name}",
                                error.into_primitive()
                            );
                            return;
                        }
                        _ => {}
                    }
                }
                present_parameters = scheduler.wait_to_update().fuse() => {
                    trace::duration!("scene_manager", "SceneManager::Present",
                                     "debug_name" => &*debug_name);
                    trace::flow_begin!("gfx", "Flatland::Present", present_count.into());
                    present_count += 1;
                    channels_awaiting_pingback.push_front(Vec::new());
                    if let Some(flatland) = weak_flatland.upgrade() {
                        flatland
                            .lock()
                            .present(present_parameters.into())
                            .expect("Present failed for {debug_name}");
                    } else {
                        fx_log_warn!(
                            "Failed to upgrade Flatand weak ref; exiting listener loop for {debug_name}"
                        );
                        return;
                    }
            }
        }
    }})
    .detach()
}

pub fn handle_pointer_injector_configuration_setup_request_stream(
    mut request_stream: PointerInjectorConfigurationSetupRequestStream,
    scene_manager: Arc<futures::lock::Mutex<dyn SceneManager>>,
) {
    fasync::Task::local(async move {
        let subscriber =
            scene_manager.lock().await.get_pointerinjector_viewport_watcher_subscription();

        loop {
            let request = request_stream.try_next().await;
            match request {
                Ok(Some(PointerInjectorConfigurationSetupRequest::GetViewRefs { responder })) => {
                    let (mut context_view_ref, mut target_view_ref) =
                        scene_manager.lock().await.get_pointerinjection_view_refs();
                    if let Err(e) = responder.send(&mut context_view_ref, &mut target_view_ref) {
                        fx_log_warn!("Failed to send GetViewRefs() response: {}", e);
                    }
                }
                Ok(Some(PointerInjectorConfigurationSetupRequest::WatchViewport { responder })) => {
                    if let Err(e) = subscriber.register(responder) {
                        fx_log_warn!("Failed to register WatchViewport() subscriber: {}", e);
                    }
                }
                Ok(None) => {
                    return;
                }
                Err(e) => {
                    fx_log_err!("Error obtaining SetupRequest: {}", e);
                    return;
                }
            }
        }
    })
    .detach()
}
