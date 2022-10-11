// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, bail, Context};
use fidl::endpoints::{self, Proxy};
use fidl_fuchsia_element as felement;
use fidl_fuchsia_math as fmath;
use fidl_fuchsia_ui_composition as ui_comp;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_scenic::flatland;
use futures::{
    future,
    stream::{self, FusedStream},
    FutureExt, StreamExt,
};

/// Width of the border around the screen.
const BORDER_WIDTH: i32 = 10;

/// Color of the background, and the border.
const BG_COLOR: ui_comp::ColorRgba =
    ui_comp::ColorRgba { red: 0.41, green: 0.24, blue: 0.21, alpha: 1.0 };

/// `View` implements the view in the Model-View-Controller sense - it allows
/// manipulation of the graphics on-screen without worrying about all the
/// low-level details.
///
/// Methods are all synchronous but may return Futures, indicating things that
/// will happen eventually.
pub struct View {
    flatland: flatland::FlatlandProxy,
    id_generator: flatland::IdGenerator,
    root_focuser: ui_views::FocuserProxy,
    viewport_size: fmath::SizeU,
    frame_transform_id: flatland::TransformId,
    window: Option<Window>,
}

struct Window {
    window_id: WindowId,
    child_view: Option<ui_views::ViewRef>,
}

impl View {
    /// Create a new WindowManager, build the UI, and attach to the given
    /// `view_creation_token`.
    pub async fn new(
        flatland: flatland::FlatlandProxy,
        mut view_creation_token: ui_views::ViewCreationToken,
    ) -> anyhow::Result<Self> {
        let (parent_viewport_watcher, parent_viewport_watcher_request) =
            endpoints::create_proxy::<flatland::ParentViewportWatcherMarker>()
                .context("creating parent_viewport_watcher")?;
        let (view_focuser, view_focuser_request) =
            endpoints::create_proxy::<ui_views::FocuserMarker>()
                .context("creating view_focuser")?;
        flatland
            .create_view2(
                &mut view_creation_token,
                &mut ui_views::ViewIdentityOnCreation::from(fuchsia_scenic::ViewRefPair::new()?),
                flatland::ViewBoundProtocols {
                    view_focuser: Some(view_focuser_request),
                    ..flatland::ViewBoundProtocols::EMPTY
                },
                parent_viewport_watcher_request,
            )
            .context("creating root view")?;

        let viewport_size = parent_viewport_watcher
            .get_layout()
            .await
            .context("calling get_layout")?
            .logical_size
            .ok_or(anyhow!("get_layout didn't return logical_size"))?;

        // Create the "layout", such as it is.
        let mut id_generator = flatland::IdGenerator::new();

        let root_transform_id = id_generator.next_transform_id();
        flatland
            .create_transform(&mut root_transform_id.clone())
            .context("creating root transform")?;

        let frame_transform_id = id_generator.next_transform_id();
        flatland
            .create_transform(&mut frame_transform_id.clone())
            .context("creating frame transform")?;
        flatland
            .add_child(&mut root_transform_id.clone(), &mut frame_transform_id.clone())
            .context("connecting frame to root")?;
        flatland
            .set_translation(
                &mut frame_transform_id.clone(),
                &mut fmath::Vec_ { x: BORDER_WIDTH, y: BORDER_WIDTH },
            )
            .context("positioning frame")?;

        flatland
            .set_root_transform(&mut root_transform_id.clone())
            .context("connecting root to scene")?;

        let root_content_id = id_generator.next_content_id();
        flatland.create_filled_rect(&mut root_content_id.clone()).context("creating desktop")?;
        flatland
            .set_content(&mut root_transform_id.clone(), &mut root_content_id.clone())
            .context("installing desktop")?;
        flatland
            .set_solid_fill(
                &mut root_content_id.clone(),
                &mut BG_COLOR.clone(),
                // TODO(fxbug.dev/110653): Mysteriously, Scenic blows up when
                // you make a rectangle the size of the viewport, under very
                // specific circumstances. When that bug is fixed, change this
                // to just `viewport_size.clone()`.
                &mut fmath::SizeU {
                    width: viewport_size.width - 1,
                    height: viewport_size.height - 1,
                },
            )
            .context("filling desktop")?;

        Ok(View {
            flatland,
            id_generator,
            root_focuser: view_focuser,
            viewport_size,
            frame_transform_id,
            window: None,
        })
    }

    /// Create a window for an application.
    ///
    /// Returns an error if there's already an open window.
    pub fn create_window(
        &mut self,
        mut viewport_creation_token: ui_views::ViewportCreationToken,
    ) -> anyhow::Result<CreateWindowResponse> {
        if self.window.is_some() {
            bail!("Only one window supported!")
        }

        let content_id = self.id_generator.next_content_id();
        let window_id = WindowId(content_id.value);

        let (child_view_watcher, child_view_watcher_server) =
            endpoints::create_proxy::<flatland::ChildViewWatcherMarker>()
                .context("creating ChildViewWatcher proxy")?;

        self.flatland
            .create_viewport(
                &mut content_id.clone(),
                &mut viewport_creation_token,
                flatland::ViewportProperties {
                    logical_size: Some(fidl_fuchsia_math::SizeU {
                        width: self.viewport_size.width - 2 * (BORDER_WIDTH as u32),
                        height: self.viewport_size.height - 2 * (BORDER_WIDTH as u32),
                    }),
                    ..flatland::ViewportProperties::EMPTY
                },
                child_view_watcher_server,
            )
            .context("creating window viewport")?;
        self.flatland
            .set_content(&mut self.frame_transform_id.clone(), &mut content_id.clone())
            .context("attaching window viewport to frame")?;

        self.window = Some(Window { window_id, child_view: None });

        // Future that resolves when the child actually attaches to the
        // viewport.
        //
        // TODO(hjfreyer@google.com): Use `child_view_watcher.get_status()` to
        // delay showing the window until the child is "ready" (i.e., it has
        // rendered its first frame).
        let on_child_view_attached = child_view_watcher
            .get_view_ref()
            .map(|res| res.context("waiting for application's view_ref"))
            .boxed_local();

        // Future that resolves when the child view is closed.
        let on_child_view_closed = async move {
            match child_view_watcher.on_closed().await {
                Ok(_) => Ok(()),
                Err(status) => Err(anyhow!("error while awaiting channel close: {:?}", status)),
            }
        }
        .boxed_local();

        Ok(CreateWindowResponse { window_id, on_child_view_attached, on_child_view_closed })
    }

    /// Associate a child `ViewRef` with the given `window_id`. This must be
    /// done before you can call functions like `focus_window`.
    pub fn register_window(
        &mut self,
        window_id: WindowId,
        child_view: ui_views::ViewRef,
    ) -> anyhow::Result<()> {
        let window = match self.window.as_mut() {
            Some(window) => window,
            None => bail!(
                "Tried to register window with ID {}, but there's no active window",
                window_id.0
            ),
        };

        if window.window_id != window_id {
            bail!(
                "Tried to register window with ID {}, but the active window has ID {}",
                window_id.0,
                window.window_id.0
            );
        }

        if window.child_view.is_some() {
            bail!(
                "Tried to associate view with window {}, which already has view {:?}",
                window_id.0,
                &window.child_view
            )
        }

        window.child_view = Some(child_view);
        Ok(())
    }

    /// Delegate focus to the view corresponding to the window with the given
    /// `window_id`. Requires `register_view` to have already been called.
    pub fn focus_window(&mut self, window_id: WindowId) -> anyhow::Result<FocusWindowResponse> {
        let window = match self.window.as_mut() {
            Some(window) => window,
            None => bail!(
                "Tried to give focus to window with ID {}, but there's no active window",
                window_id.0
            ),
        };

        if window.window_id != window_id {
            bail!(
                "Tried to give focus to window with ID {}, but the active window has ID {}",
                window_id.0,
                window.window_id.0
            );
        }

        let child_view = match window.child_view.as_ref() {
            Some(child_view) => child_view,
            None => bail!(
                "Tried to give focus to window with ID {}, but it hasn't been registered",
                window_id.0
            ),
        };

        let set_auto_focus_result =
            self.root_focuser.set_auto_focus(ui_views::FocuserSetAutoFocusRequest {
                view_ref: Some(fuchsia_scenic::duplicate_view_ref(child_view)?),
                ..ui_views::FocuserSetAutoFocusRequest::EMPTY
            });

        let on_completed = async move {
            set_auto_focus_result
                .await
                .context("setting auto_focus")?
                .map_err(|err| anyhow!("auto focus error: {:?}", err))
        }
        .boxed_local();

        Ok(FocusWindowResponse { on_completed })
    }

    /// Dismiss the window with the given `window_id`.
    pub fn dismiss_window(&mut self, window_id: WindowId) -> anyhow::Result<DismissWindowResponse> {
        let window = match self.window.as_ref() {
            Some(window) => window,
            None => bail!(
                "Tried to dismiss window with ID {}, but there's no active window",
                window_id.0
            ),
        };

        if window.window_id != window_id {
            bail!(
                "Tried to dismiss window with ID {}, but the active window has ID {}",
                window_id.0,
                window.window_id.0
            );
        }

        self.flatland
            .set_content(&mut self.frame_transform_id.clone(), &mut ui_comp::ContentId { value: 0 })
            .context("detaching window viewport from frame")?;

        let release_viewport_result =
            self.flatland.release_viewport(&mut window_id.into_content_id());

        let on_completed = async move {
            let _token: ui_views::ViewportCreationToken =
                release_viewport_result.await.context("while releasing viewport")?;
            Ok(())
        }
        .boxed_local();

        self.window = None;
        Ok(DismissWindowResponse { on_completed })
    }

    pub fn active_window_id(&self) -> Option<WindowId> {
        let window = self.window.as_ref()?;
        Some(window.window_id)
    }
}

#[derive(Debug, Copy, Clone, Hash, Ord, PartialOrd, Eq, PartialEq)]
pub struct WindowId(u64);

impl WindowId {
    pub fn into_content_id(self) -> ui_comp::ContentId {
        ui_comp::ContentId { value: self.0 }
    }
}

/// Response for the `create_window` call.
pub struct CreateWindowResponse {
    /// ID for the window that was created.
    pub window_id: WindowId,

    /// A future that resolves when a child view has actually been attached to
    /// the window. This `ViewRef` should then be passed back into
    /// `register_window`, so we can do things like give it focus.
    pub on_child_view_attached: future::LocalBoxFuture<'static, anyhow::Result<ui_views::ViewRef>>,

    /// A future that resolves when the `ChildViewWatcher` associated with the
    /// window closes.
    pub on_child_view_closed: future::LocalBoxFuture<'static, anyhow::Result<()>>,
}

/// Response for the `focus_window` call.
pub struct FocusWindowResponse {
    /// A Future indicating the success/failure of the call.
    pub on_completed: future::LocalBoxFuture<'static, anyhow::Result<()>>,
}

/// Response for the `dismiss_window` call.
pub struct DismissWindowResponse {
    /// A Future indicating the success/failure of the call.
    pub on_completed: future::LocalBoxFuture<'static, anyhow::Result<()>>,
}

/// `Manager` implements business logic for the window manager. It responds to
/// user requests, manipulates the `View`, and handles a queue of background
/// tasks.
///
/// Users of `Manager` must call `select_background_task` regularly to drive
/// internal background tasks and observe any errors going on in the background.
pub struct Manager {
    view: View,
    background_tasks: stream::FuturesUnordered<
        future::LocalBoxFuture<'static, Box<dyn FnOnce(&mut Manager) -> anyhow::Result<()>>>,
    >,
}

impl Manager {
    /// Create a new `Manager` that manipulates the given `View`.
    pub fn new(view: View) -> Self {
        Self { view, background_tasks: stream::FuturesUnordered::new() }
    }

    /// Handle a `GraphicalPresenter::PresentView` request.
    //
    // TODO(hjfreyer@google.com): Consider supporting applications that don't
    // supply a `view_controller_request`.
    pub fn present_view(
        &mut self,
        view_spec: felement::ViewSpec,
        _annotation_controller: Option<endpoints::ClientEnd<felement::AnnotationControllerMarker>>,
        view_controller_request: Option<endpoints::ServerEnd<felement::ViewControllerMarker>>,
    ) -> anyhow::Result<()> {
        let viewport_creation_token = view_spec
            .viewport_creation_token
            .ok_or(anyhow!("view_spec didn't include viewport_creation_token"))?;

        let view_controller_server = view_controller_request
            .ok_or(anyhow!("request didn't include view_controller_request"))?;

        // Dismiss the existing window, if any.
        if let Some(window_id) = self.view.active_window_id() {
            let dismiss_window_response = self.view.dismiss_window(window_id)?;
            self.background_result(dismiss_window_response.on_completed);
        }

        let CreateWindowResponse { window_id, on_child_view_attached, on_child_view_closed } =
            self.view.create_window(viewport_creation_token)?;

        // Register the child view once it is attached to the window.
        self.and_then_background_task(
            on_child_view_attached,
            move |this: &mut Manager, child_view_ref: ui_views::ViewRef| {
                if this.view.active_window_id() != Some(window_id) {
                    tracing::warn!(
                        "Trying to register child view for {:?}, but the active window is {:?}",
                        window_id,
                        this.view.active_window_id()
                    );
                    return Ok(());
                }

                this.view.register_window(window_id, child_view_ref)?;
                let response = this.view.focus_window(window_id)?;

                this.background_result(response.on_completed);
                Ok(())
            },
        );

        // A Future that resolves with `true` when the `ViewController` client
        // calls `ViewController::Dismiss`, or `false` if the channel closes
        // without a call to `Dismiss`. Logs any errors encountered along the
        // way.
        //
        // We observe this, and call `View::dismiss_window` when requested.
        let was_dismissed = view_controller_server.into_stream()?.any(|request| match request {
            Ok(felement::ViewControllerRequest::Dismiss { .. }) => futures::future::ready(true),
            Err(err) => {
                tracing::warn!("while reading ViewController request: {}", err);
                futures::future::ready(false)
            }
        });

        self.background_task(was_dismissed, move |this: &mut Manager, was_dismissed| {
            if was_dismissed && this.view.active_window_id() == Some(window_id) {
                tracing::info!("Dismiss for window {:?} requested", window_id);
                let dismiss_window_response = this.view.dismiss_window(window_id)?;
                this.background_result(dismiss_window_response.on_completed);
            }
            Ok(())
        });

        // We also dismiss and clean-up the window if the child view is closed.
        self.and_then_background_task(on_child_view_closed, move |this: &mut Manager, ()| {
            if this.view.active_window_id() != Some(window_id) {
                return Ok(());
            }
            let dismiss_window_response = this.view.dismiss_window(window_id)?;
            this.background_result(dismiss_window_response.on_completed);
            Ok(())
        });

        Ok(())
    }

    /// A Future that resolves when the next background task has been completed.
    /// The Future returns a `Result` indicating whether the background task
    /// succeeded or failed. If there is no background work, this blocks
    /// forever.
    ///
    /// This is intended to be used in a `select!{}` block.
    pub fn select_background_task(&mut self) -> SelectBackgroundTask<'_> {
        return SelectBackgroundTask { manager: self };
    }

    /// Enqueues a background task from a Future and a closure. Note that `fut`
    /// has a static lifetime, and therefore cannot depend on `self`. The
    /// closure takes a `&mut Manager` and the output of `fut`.
    ///
    /// `fut` will be polled whenever the result of `select_background_task` is
    /// polled, and once `fut` completes, `work` will be called on the result.
    fn background_task<Fut, Work>(&mut self, fut: Fut, work: Work)
    where
        Fut: futures::Future + 'static,
        Work: FnOnce(&mut Manager, Fut::Output) -> anyhow::Result<()> + 'static,
    {
        self.background_tasks.push(
            fut.map(|res| -> Box<dyn FnOnce(&mut Manager) -> anyhow::Result<()>> {
                Box::new(move |wrapper: &mut Manager| -> anyhow::Result<()> { work(wrapper, res) })
            })
            .boxed_local(),
        );
    }

    /// Version of `background_task` that passes through any errors returned by
    /// `fut`. This is to `background_task` what `and_then` is to `map`.
    fn and_then_background_task<Ok, Fut, Work>(&mut self, fut: Fut, work: Work)
    where
        Fut: futures::Future<Output = anyhow::Result<Ok>> + 'static,
        Work: FnOnce(&mut Manager, Ok) -> anyhow::Result<()> + 'static,
    {
        self.background_task(fut, |this, result| {
            let ok = result?;
            work(this, ok)
        })
    }

    /// Version of `background_task` that does no work.
    /// `select_background_task()` simply polls `fut` and then returns the
    /// result once it's done.
    fn background_result<Fut>(&mut self, fut: Fut)
    where
        Fut: futures::Future<Output = anyhow::Result<()>> + 'static,
    {
        self.background_task(fut, |_, result| result)
    }
}

/// Future for the `select_background_task` method.
///
/// Based on `futures::stream::SelectNextSome`.
pub struct SelectBackgroundTask<'a> {
    manager: &'a mut Manager,
}

impl<'a> future::FusedFuture for SelectBackgroundTask<'a> {
    fn is_terminated(&self) -> bool {
        self.manager.background_tasks.is_terminated()
    }
}

impl<'a> futures::Future for SelectBackgroundTask<'a> {
    type Output = anyhow::Result<()>;

    fn poll(
        mut self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
    ) -> std::task::Poll<Self::Output> {
        assert!(
            !self.manager.background_tasks.is_terminated(),
            "SelectBackgroundTask polled after terminated"
        );

        if let Some(item) = futures::ready!((*self).manager.background_tasks.poll_next_unpin(cx)) {
            std::task::Poll::Ready(item(&mut self.manager))
        } else {
            debug_assert!(self.manager.background_tasks.is_terminated());
            cx.waker().wake_by_ref();
            std::task::Poll::Pending
        }
    }
}

#[cfg(test)]
mod tests {
    use anyhow::Error;
    use fidl::endpoints::{self, Proxy};
    use fidl_fuchsia_element as felement;
    use fidl_fuchsia_ui_app as ui_app;
    use fidl_fuchsia_ui_composition as ui_comp;
    use fidl_fuchsia_ui_test_scene as ui_test_scene;
    use fidl_fuchsia_ui_views as ui_views;
    use fuchsia_async as fasync;
    use fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route};
    use fuchsia_scenic::flatland;
    use futures::{select, StreamExt};

    use super::*;

    async fn await_next_on_frame_begin(events: &mut flatland::FlatlandEventStream) {
        loop {
            match events.next().await.unwrap().unwrap() {
                ui_comp::FlatlandEvent::OnNextFrameBegin { .. } => return,
                ui_comp::FlatlandEvent::OnFramePresented { .. } => (),
                ui_comp::FlatlandEvent::OnError { error } => {
                    panic!("flatland error: {:?}", error)
                }
            }
        }
    }

    #[fuchsia::test]
    async fn test_wm_create_and_dismiss() -> Result<(), Error> {
        let builder = RealmBuilder::new().await?;

        let test_ui_stack = builder
            .add_child("test-ui-stack", "#meta/test-ui-stack.cm", ChildOptions::new())
            .await?;

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .capability(Capability::protocol_by_name("fuchsia.scheduler.ProfileProvider"))
                    .capability(Capability::protocol_by_name("fuchsia.sysmem.Allocator"))
                    .capability(Capability::protocol_by_name("fuchsia.tracing.provider.Registry"))
                    .capability(Capability::protocol_by_name("fuchsia.vulkan.loader.Loader"))
                    .from(Ref::parent())
                    .to(&test_ui_stack),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.ui.test.scene.Controller"))
                    .capability(Capability::protocol_by_name("fuchsia.ui.composition.Flatland"))
                    .from(&test_ui_stack)
                    .to(Ref::parent()),
            )
            .await?;

        let realm = builder.build().await?;
        let flatland =
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?;
        let scene_controller =
            realm.root.connect_to_protocol_at_exposed_dir::<ui_test_scene::ControllerMarker>()?;

        let (graphical_presenter_proxy, graphical_presenter_request_stream) =
            endpoints::create_proxy_and_stream::<felement::GraphicalPresenterMarker>()?;

        // A test server that loops on every frame.
        async fn test_server(
            flatland: flatland::FlatlandProxy,
            scene_controller: ui_test_scene::ControllerProxy,
            mut graphical_presenter_request_stream: felement::GraphicalPresenterRequestStream,
        ) {
            let (view_provider, view_provider_request_stream) =
                endpoints::create_request_stream::<ui_app::ViewProviderMarker>()
                    .expect("failed to create ViewProvider request stream");

            fasync::Task::spawn(async move {
                let _view_ref_koid = scene_controller
                    .attach_client_view(ui_test_scene::ControllerAttachClientViewRequest {
                        view_provider: Some(view_provider),
                        ..ui_test_scene::ControllerAttachClientViewRequest::EMPTY
                    })
                    .await
                    .expect("failed to attach root client view");
            })
            .detach();

            // Set up the window manager.
            let view_creation_token = view_provider_request_stream
                .map(|request| {
                    if let Ok(ui_app::ViewProviderRequest::CreateView2 { args, .. }) = request {
                        args.view_creation_token.unwrap()
                    } else {
                        panic!("Unexpected request: {:?}", request)
                    }
                })
                .into_future()
                .await
                .0
                .unwrap();

            let mut server =
                Manager::new(View::new(flatland.clone(), view_creation_token).await.unwrap());
            let mut flatland_events = flatland.take_event_stream();

            loop {
                flatland.present(flatland::PresentArgs::EMPTY).unwrap();

                await_next_on_frame_begin(&mut flatland_events).await;
                select! {
                    req = graphical_presenter_request_stream.next() => {
                        if req.is_none() {
                            return
                        }

                        let felement::GraphicalPresenterRequest::PresentView {
                            view_spec,
                            annotation_controller,
                            view_controller_request,
                            responder,
                        } = req.unwrap().unwrap();
                        server.present_view(
                            view_spec,
                            annotation_controller,
                            view_controller_request
                        )
                        .unwrap();
                        responder.send(&mut Ok(())).unwrap();
                    }
                    bg = server.select_background_task() => {
                        bg.unwrap();
                    }
                }
            }
        }

        async fn test_client(
            flatland2: flatland::FlatlandProxy,
            graphical_presenter_proxy: felement::GraphicalPresenterProxy,
        ) {
            // Create a window.
            let flatland::ViewCreationTokenPair {
                mut view_creation_token,
                viewport_creation_token,
            } = flatland::ViewCreationTokenPair::new().unwrap();

            let (view_controller_client, view_controller_server) =
                endpoints::create_proxy::<felement::ViewControllerMarker>().unwrap();

            let view_spec = felement::ViewSpec {
                viewport_creation_token: Some(viewport_creation_token),
                ..felement::ViewSpec::EMPTY
            };

            let () = graphical_presenter_proxy
                .present_view(view_spec, None, Some(view_controller_server))
                .await
                .unwrap()
                .unwrap();

            let (parent_viewport_watcher, parent_viewport_watcher_request) =
                endpoints::create_proxy::<flatland::ParentViewportWatcherMarker>().unwrap();

            let (view_ref_focused_proxy, view_ref_focused_server) =
                endpoints::create_proxy::<fidl_fuchsia_ui_views::ViewRefFocusedMarker>().unwrap();

            flatland2
                .create_view2(
                    &mut view_creation_token,
                    &mut ui_views::ViewIdentityOnCreation::from(
                        fuchsia_scenic::ViewRefPair::new().unwrap(),
                    ),
                    flatland::ViewBoundProtocols {
                        view_ref_focused: Some(view_ref_focused_server),
                        ..flatland::ViewBoundProtocols::EMPTY
                    },
                    parent_viewport_watcher_request,
                )
                .unwrap();

            let mut events = flatland2.take_event_stream();
            flatland2.present(flatland::PresentArgs::EMPTY).unwrap();
            await_next_on_frame_begin(&mut events).await;

            // Wait for the child to be attached.
            while parent_viewport_watcher.get_status().await.unwrap()
                != ui_comp::ParentViewportStatus::ConnectedToDisplay
            {}

            // Wait for the child to get focus.
            while view_ref_focused_proxy.watch().await.unwrap().focused != Some(true) {}

            // Dismiss the view, and wait for the parent_viewport_watcher to close.
            view_controller_client.dismiss().unwrap();
            parent_viewport_watcher.on_closed().await.unwrap();
        }

        let server_task = fasync::Task::local(test_server(
            flatland,
            scene_controller,
            graphical_presenter_request_stream,
        ));

        let client_task = fasync::Task::local(test_client(
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?,
            graphical_presenter_proxy,
        ));

        // client_task completing deletes the `GraphicalPresenterProxy`, which
        // tells the server to shut down.
        client_task.await;
        server_task.await;
        realm.destroy().await?;
        Ok(())
    }
}
