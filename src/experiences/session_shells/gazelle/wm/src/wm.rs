// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections;

use anyhow::{anyhow, bail, Context};
use fidl::endpoints::{self, Proxy};
use fidl_fuchsia_element as felement;
use fidl_fuchsia_math as fmath;
use fidl_fuchsia_ui_composition as ui_comp;
use fidl_fuchsia_ui_input3 as ui_input3;
use fidl_fuchsia_ui_shortcut2 as ui_shortcut2;
use fidl_fuchsia_ui_views as ui_views;
use fuchsia_scenic::flatland;
use futures::{
    future, select,
    stream::{self, FusedStream},
    FutureExt, StreamExt,
};

/// Width of the border around the screen.
const BORDER_WIDTH: i32 = 10;

/// Color of the background, and the border.
const BG_COLOR: ui_comp::ColorRgba =
    ui_comp::ColorRgba { red: 0.41, green: 0.24, blue: 0.21, alpha: 1.0 };

/// Shortcut ID registered for "cycle through windows" a.k.a. Alt-Tab.
const CYCLE_WINDOWS_SHORTCUT_ID: u32 = 1;

/// `View` implements the view in the Model-View-Controller sense - it allows
/// manipulation of the graphics on-screen without worrying about all the
/// low-level details.
///
/// A `View` may have multiple simultaneous windows, but only one window is
/// "active" at any given time. The active window is attached to the scene and
/// given focus whenever possible.
///
/// Methods are all synchronous but may return static-lifetime Futures,
/// indicating things that will happen eventually.
pub struct View {
    flatland: flatland::FlatlandProxy,

    view_ref: ui_views::ViewRef,
    id_generator: flatland::IdGenerator,
    desktop_content_id: flatland::ContentId,
    frame_transform_id: flatland::TransformId,

    /// ParentViewportWatcher for the viewport to which the overall window
    /// manager is attached (not the viewport for any particular window).
    parent_viewport_watcher: ui_comp::ParentViewportWatcherProxy,
    root_focuser: ui_views::FocuserProxy,
    viewport_size: Option<fmath::SizeU>,

    /// All the open windows. At most one of them is `active` at any given time.
    windows: collections::BTreeMap<WindowId, Window>,

    /// Provides an ordering over all windows. The `back` entry is the active
    /// one.
    window_order: collections::VecDeque<WindowId>,
}

struct Window {
    child_view: Option<ui_views::ViewRef>,
}

impl View {
    /// Create a new WindowManager, build the UI, and attach to the given
    /// `view_creation_token`.
    pub fn new(
        flatland: flatland::FlatlandProxy,
        mut view_creation_token: ui_views::ViewCreationToken,
    ) -> anyhow::Result<Self> {
        let (parent_viewport_watcher, parent_viewport_watcher_request) =
            endpoints::create_proxy::<flatland::ParentViewportWatcherMarker>()
                .context("creating parent_viewport_watcher")?;
        let (view_focuser, view_focuser_request) =
            endpoints::create_proxy::<ui_views::FocuserMarker>()
                .context("creating view_focuser")?;

        let mut view_ref_pair =
            ui_views::ViewIdentityOnCreation::from(fuchsia_scenic::ViewRefPair::new()?);

        let view_ref = fuchsia_scenic::duplicate_view_ref(&view_ref_pair.view_ref)?;

        flatland
            .create_view2(
                &mut view_creation_token,
                &mut view_ref_pair,
                flatland::ViewBoundProtocols {
                    view_focuser: Some(view_focuser_request),
                    ..flatland::ViewBoundProtocols::EMPTY
                },
                parent_viewport_watcher_request,
            )
            .context("creating root view")?;

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

        let desktop_content_id = id_generator.next_content_id();
        flatland.create_filled_rect(&mut desktop_content_id.clone()).context("creating desktop")?;
        flatland
            .set_content(&mut root_transform_id.clone(), &mut desktop_content_id.clone())
            .context("attaching desktop")?;

        Ok(View {
            flatland,
            view_ref,
            id_generator,
            desktop_content_id,
            frame_transform_id,
            parent_viewport_watcher,
            root_focuser: view_focuser,
            viewport_size: None,
            windows: collections::BTreeMap::new(),
            window_order: collections::VecDeque::new(),
        })
    }

    /// The set of currently open windows.
    pub fn window_ids(&self) -> Vec<WindowId> {
        self.window_order.iter().copied().collect()
    }

    /// Returns a duplicated ViewRef for the Window Manager as a whole.
    pub fn get_view_ref(&self) -> anyhow::Result<ui_views::ViewRef> {
        fuchsia_scenic::duplicate_view_ref(&self.view_ref)
            .context("duplicating Window Manager ViewRef")
    }

    /// Hanging get for updates to the parent viewport's logical size.
    pub fn query_viewport_size(
        &self,
    ) -> future::LocalBoxFuture<'static, anyhow::Result<fmath::SizeU>> {
        self.parent_viewport_watcher
            .get_layout()
            .map(|response| {
                response
                    .context("calling get_layout")?
                    .logical_size
                    .ok_or(anyhow!("get_layout didn't return logical_size"))
            })
            .boxed_local()
    }

    /// Updates the viewport's logical size.
    ///
    /// Updates do not take effect until `update` is called.
    pub fn set_viewport_size(&mut self, viewport_size: fmath::SizeU) {
        self.viewport_size = Some(viewport_size);
    }

    /// Create a window for an application.
    ///
    /// Updates do not take effect until `update` is called. Panics unless an
    /// initial `viewport_size` has already been specified via
    /// `set_viewport_size`.
    pub fn create_window(
        &mut self,
        mut viewport_creation_token: ui_views::ViewportCreationToken,
    ) -> anyhow::Result<CreateWindowResponse> {
        let content_id = self.id_generator.next_content_id();
        let window_id = WindowId(content_id.value);

        tracing::info!("create_window {:?}", window_id);
        let viewport_size =
            self.viewport_size.expect("called create_window before setting initial viewport_size");

        let (child_view_watcher, child_view_watcher_server) =
            endpoints::create_proxy::<flatland::ChildViewWatcherMarker>()
                .context("creating ChildViewWatcher proxy")?;

        self.flatland
            .create_viewport(
                &mut content_id.clone(),
                &mut viewport_creation_token,
                flatland::ViewportProperties {
                    logical_size: Some(Self::window_size(&viewport_size)),
                    ..flatland::ViewportProperties::EMPTY
                },
                child_view_watcher_server,
            )
            .context("creating window viewport")?;

        self.windows.insert(window_id, Window { child_view: None });
        self.window_order.push_back(window_id);

        // Future that resolves when the child actually attaches to the
        // viewport.
        //
        // TODO(hjfreyer@google.com): Use `child_view_watcher.get_status()` to
        // delay showing the window until the child is "ready" (i.e., it has
        // rendered its first frame).
        let on_child_view_attached = child_view_watcher
            .get_view_ref()
            .map(|res| match res {
                Ok(view_ref) => Ok(Some(view_ref)),
                Err(err) if err.is_closed() => Ok(None),
                Err(err) => Err(err).context("waiting for application's view_ref"),
            })
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

    /// Associate a child `ViewRef` with the given `window_id`. Until this is
    /// called, focus cannot be automatically transferred to the window.
    pub fn register_window(
        &mut self,
        window_id: WindowId,
        child_view: ui_views::ViewRef,
    ) -> anyhow::Result<()> {
        tracing::info!("register_window {:?}", window_id);
        let window = match self.windows.get_mut(&window_id) {
            Some(window) => window,
            None => bail!("Tried to register window {:?}, but there's no such window", window_id),
        };

        if window.child_view.is_some() {
            bail!(
                "Tried to associate view with window {:?}, which already has view {:?}",
                window_id,
                &window.child_view
            )
        }

        window.child_view = Some(child_view);
        Ok(())
    }

    /// Dismiss the window with the given `window_id`.
    pub fn dismiss_window(&mut self, window_id: WindowId) -> anyhow::Result<DismissWindowResponse> {
        tracing::info!("dismiss_window {:?}", window_id);
        if self.windows.remove(&window_id).is_none() {
            bail!("Tried to dismiss window {:?}, but there's no such window", window_id)
        };
        self.window_order.retain(|w| w != &window_id);

        let release_viewport_result =
            self.flatland.release_viewport(&mut window_id.into_content_id());

        let on_completed = async move {
            let _token: ui_views::ViewportCreationToken =
                release_viewport_result.await.context("while releasing viewport")?;
            Ok(())
        }
        .boxed_local();

        Ok(DismissWindowResponse { on_completed })
    }

    /// Updates the flatland view hierarchy to match the View's model. Other
    /// functions in this class generally only change the internal model; use
    /// this to publish those changes to flatland.
    ///
    /// Note: this does not actually  call `flatland::present`, so generally
    /// you'll want to call other functions to update the View's model, then
    /// `update()` to push the changes to flatland, then `present()` to actually
    /// show those changes on the screen.
    ///
    /// Panics unless an initial `viewport_size` has already been specified via
    /// `set_viewport_size`.
    pub fn update(&self) -> anyhow::Result<UpdateResponse> {
        tracing::info!("update");
        let viewport_size =
            self.viewport_size.expect("called update before setting initial viewport_size");

        self.flatland
            .set_solid_fill(
                &mut self.desktop_content_id.clone(),
                &mut BG_COLOR.clone(),
                &mut Self::desktop_size(&viewport_size),
            )
            .context("filling desktop")?;

        match self.window_order.back() {
            None => {
                // There aren't any windows - just draw the desktop.
                self.flatland
                    .set_content(
                        &mut self.frame_transform_id.clone(),
                        &mut ui_comp::ContentId { value: 0 },
                    )
                    .context("detaching window viewport from frame")?;

                let set_auto_focus_result =
                    self.root_focuser.set_auto_focus(ui_views::FocuserSetAutoFocusRequest::EMPTY);

                let on_completed = async move {
                    set_auto_focus_result
                        .await
                        .context("setting auto_focus")?
                        .map_err(|err| anyhow!("auto focus error: {:?}", err))
                }
                .boxed_local();

                Ok(UpdateResponse { on_completed })
            }
            Some(active_window_id) => {
                // There are windows - draw the active one.
                let active_window = self
                    .windows
                    .get(active_window_id)
                    .expect("windows and window_order fell out of sync");

                // Only update the the viewport properties (notably the
                // logical_size, which may have changed since the last update)
                // if the active window actually has a child view. If the child
                // view isn't present, scenic currently crashes when you try to
                // update the viewport.
                //
                // TODO(fxbug.dev/112339): Update the viewport unconditionally,
                // because this logic is a bit arbitrary.
                if active_window.child_view.is_some() {
                    self.flatland
                        .set_viewport_properties(
                            &mut active_window_id.into_content_id(),
                            flatland::ViewportProperties {
                                logical_size: Some(Self::window_size(&viewport_size)),
                                ..flatland::ViewportProperties::EMPTY
                            },
                        )
                        .context("creating window viewport")?;
                }

                self.flatland
                    .set_content(
                        &mut self.frame_transform_id.clone(),
                        &mut active_window_id.into_content_id(),
                    )
                    .context("attaching window viewport to frame")?;

                let set_auto_focus_result =
                    self.root_focuser.set_auto_focus(ui_views::FocuserSetAutoFocusRequest {
                        view_ref: match active_window.child_view.as_ref() {
                            None => None,
                            Some(child_view) => {
                                Some(fuchsia_scenic::duplicate_view_ref(child_view)?)
                            }
                        },
                        ..ui_views::FocuserSetAutoFocusRequest::EMPTY
                    });

                let on_completed = async move {
                    set_auto_focus_result
                        .await
                        .context("setting auto_focus")?
                        .map_err(|err| anyhow!("auto focus error: {:?}", err))
                }
                .boxed_local();

                Ok(UpdateResponse { on_completed })
            }
        }
    }

    /// Moves the active (back) window to the front of the window ordering.
    /// Repeated calls to this will cycle through the windows, making each the
    /// active window, in turn.
    ///
    /// If there are fewer that 2 windows open, this does nothing.
    pub fn cycle_windows(&mut self) {
        tracing::info!("cycle_windows");
        if let Some(active) = self.window_order.pop_back() {
            self.window_order.push_front(active);
        }
    }

    /// Calculates the size of the desktop background, based on the given
    /// viewport_size.
    fn desktop_size(viewport_size: &fmath::SizeU) -> fmath::SizeU {
        // TODO(fxbug.dev/110653): Mysteriously, Scenic blows up when
        // you make a rectangle the size of the viewport, under very
        // specific circumstances. When that bug is fixed, change this
        // to just `viewport_size.clone()`.
        fmath::SizeU {
            width: viewport_size.width.saturating_sub(1).clamp(1, u32::MAX),
            height: viewport_size.height.saturating_sub(1).clamp(1, u32::MAX),
        }
    }

    /// Calculates the size of the window, based on the given viewport_size.
    fn window_size(viewport_size: &fmath::SizeU) -> fmath::SizeU {
        fidl_fuchsia_math::SizeU {
            width: viewport_size.width.saturating_sub(2 * (BORDER_WIDTH as u32)).clamp(1, u32::MAX),
            height: viewport_size
                .height
                .saturating_sub(2 * (BORDER_WIDTH as u32))
                .clamp(1, u32::MAX),
        }
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
#[must_use]
pub struct CreateWindowResponse {
    /// ID for the window that was created.
    pub window_id: WindowId,

    /// A future that resolves when a child view has actually been attached to
    /// the window. This `ViewRef` should then be passed back into
    /// `register_window`, so we can do things like give it focus.
    ///
    /// If the channel closes without providing a ViewRef, this resolves to
    /// None.
    pub on_child_view_attached:
        future::LocalBoxFuture<'static, anyhow::Result<Option<ui_views::ViewRef>>>,

    /// A future that resolves when the `ChildViewWatcher` associated with the
    /// window closes.
    pub on_child_view_closed: future::LocalBoxFuture<'static, anyhow::Result<()>>,
}

/// Response for the `dismiss_window` call.
#[must_use]
pub struct DismissWindowResponse {
    /// A Future indicating the success/failure of the call.
    pub on_completed: future::LocalBoxFuture<'static, anyhow::Result<()>>,
}

/// Response for the `update` call.
#[must_use]
pub struct UpdateResponse {
    /// A Future indicating the success/failure of the call.
    pub on_completed: future::LocalBoxFuture<'static, anyhow::Result<()>>,
}

/// `Manager` implements business logic for the window manager. It responds to
/// user requests, manipulates the `View`, and handles a queue of background
/// tasks.
///
/// Users of `Manager` must call `select_background_task` regularly to drive
/// internal background tasks. They should also periodically call
/// `take_background_results()` and poll the resulting stream in order to
/// observe any errors going on in the background.
pub struct Manager {
    view: View,

    /// Tasks that may need to modify the manager once they are complete. This
    /// is for things like keyboard events. We don't necessarily know that these
    /// are going to complete at any point.
    background_tasks: stream::FuturesUnordered<
        future::LocalBoxFuture<'static, Box<dyn FnOnce(&mut Manager) -> anyhow::Result<()>>>,
    >,

    /// Asynchronous results of calls that we have made. The manager doesn't
    /// take any action as result of these futures, and strictly speaking they
    /// don't even need to get polled. However, they're provided to the caller
    /// for logging or panicking when errors are encountered.
    background_results:
        stream::FuturesUnordered<future::LocalBoxFuture<'static, anyhow::Result<()>>>,
}

/// Configuration options for `Manager`.
#[derive(Debug, Clone)]
pub struct ManagerOptions {
    /// Chord of keys that, when pressed, will cause the windows to be cycled
    /// through.
    pub cycle_windows_shortcut: Vec<ui_input3::KeyMeaning>,
}

impl Manager {
    /// Create a new `Manager` that manipulates the given `View`.
    pub async fn new(
        mut view: View,
        shortcuts: ui_shortcut2::RegistryProxy,
        options: ManagerOptions,
    ) -> anyhow::Result<Self> {
        let viewport_size = view.query_viewport_size().await.context("getting viewport size")?;

        view.set_viewport_size(viewport_size);

        let update_response = view.update()?;
        update_response.on_completed.await?;

        let (listener_client, listener_requests) =
            endpoints::create_request_stream::<ui_shortcut2::ListenerMarker>()
                .context("creating shortcut listener")?;

        shortcuts
            .set_view(&mut view.get_view_ref()?, listener_client)
            .context("associating ViewRef with shortcut registry")?;

        shortcuts
            .register_shortcut(&mut ui_shortcut2::Shortcut {
                id: CYCLE_WINDOWS_SHORTCUT_ID,
                key_meanings: options.cycle_windows_shortcut,
                options: ui_shortcut2::Options::EMPTY,
            })
            .await?
            .map_err(|e| anyhow!("shortcut error while registering: {:?}", e))?;

        let mut res = Self {
            view,
            background_tasks: stream::FuturesUnordered::new(),
            background_results: stream::FuturesUnordered::new(),
        };

        res.background_stream(listener_requests, Self::handle_shortcut_event);

        Ok(res)
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

        let mut view_controller_request_stream = view_controller_request
            .ok_or(anyhow!("request didn't include view_controller_request"))?
            .into_stream()?;
        let CreateWindowResponse { window_id, on_child_view_attached, on_child_view_closed } =
            self.view.create_window(viewport_creation_token)?;

        // Register the child view once it is attached to the window.
        self.and_then_background_task(
            on_child_view_attached,
            move |this: &mut Manager, child_view_ref: Option<ui_views::ViewRef>| {
                // The window might already be closed, and that's okay.
                if !this.view.window_ids().contains(&window_id) {
                    tracing::warn!(
                        "Tried to attach view to window {:?}, which doesn't exist anymore",
                        window_id
                    );
                    return Ok(());
                }

                let child_view_ref = match child_view_ref {
                    None => {
                        tracing::warn!(
                            "channel closed when awaiting view_ref for window {:?}",
                            window_id
                        );
                        return Ok(());
                    }
                    Some(child_view_ref) => child_view_ref,
                };

                this.view.register_window(window_id, child_view_ref)?;

                let update_response = this.view.update()?;
                this.background_result(update_response.on_completed);

                Ok(())
            },
        );

        // A Future that resolves when the window should be dismissed. That is
        // to say, when one of two things happens:
        //
        // 1. the `ViewController` client calls `ViewController::Dismiss`, or
        // 2. the child view is closed.
        let on_dismissed = async move {
            let mut on_child_view_closed = on_child_view_closed.fuse();
            loop {
                select! {
                    req = view_controller_request_stream.select_next_some() => {
                        match req {
                            Ok(felement::ViewControllerRequest::Dismiss { .. }) => return,
                            Err(err) => {
                                tracing::warn!("while reading ViewController request: {}", err);
                                continue
                            }
                        }
                    }
                    _ = on_child_view_closed => return,
                }
            }
        };

        self.background_task(on_dismissed, move |this: &mut Manager, ()| {
            let dismiss_response = this.view.dismiss_window(window_id)?;
            this.background_result(dismiss_response.on_completed);

            let update_response = this.view.update()?;
            this.background_result(update_response.on_completed);

            Ok(())
        });

        let update_response = self.view.update()?;
        self.background_result(update_response.on_completed);

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

    /// Take a Stream of Results of calls that the Manager has made to backends
    /// since the last time `take_background_results` was called.
    pub fn take_background_results(
        &mut self,
    ) -> impl stream::FusedStream<Item = anyhow::Result<()>> {
        std::mem::take(&mut self.background_results)
    }

    /// Handles an event received from the Shortcuts service.
    fn handle_shortcut_event(
        &mut self,
        event: fidl::Result<ui_shortcut2::ListenerRequest>,
    ) -> anyhow::Result<()> {
        tracing::info!("handle_shortcut_event");
        match event.context("handling shortcut event")? {
            ui_shortcut2::ListenerRequest::OnShortcut {
                id: CYCLE_WINDOWS_SHORTCUT_ID,
                responder,
            } => {
                responder
                    .send(ui_shortcut2::Handled::Handled)
                    .context("replying to shortcut event")?;
                self.view.cycle_windows();

                let update_response = self.view.update()?;
                self.background_result(update_response.on_completed);

                Ok(())
            }
            ui_shortcut2::ListenerRequest::OnShortcut { id, responder } => {
                tracing::error!("Unknown shortcut ID: {}", id);
                responder
                    .send(ui_shortcut2::Handled::NotHandled)
                    .context("replying to shortcut event")
            }
        }
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

    /// Observes a Stream in the background, calling `work` for each Item
    /// emitted by the stream.
    ///
    /// This is the same as `background_task`, but for Streams.
    fn background_stream<Stream, Work>(&mut self, stream: Stream, mut work: Work)
    where
        Stream: futures::Stream + Unpin + 'static,
        Work: FnMut(&mut Manager, Stream::Item) -> anyhow::Result<()> + 'static,
    {
        self.background_task(
            stream.into_future(),
            move |this, (maybe_item, rest)| match maybe_item {
                None => Ok(()),
                Some(item) => {
                    let result = work(this, item);
                    this.background_stream(rest, work);
                    result
                }
            },
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

    /// Sends a future that reports the result of an async operation to the
    /// `background_results` stream. Clients can observe it by calling
    /// `take_background_results`.
    fn background_result<Fut>(&mut self, fut: Fut)
    where
        Fut: futures::Future<Output = anyhow::Result<()>> + 'static,
    {
        self.background_results.push(fut.boxed_local())
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
    use fidl_fuchsia_ui_input3 as ui_input3;
    use fidl_fuchsia_ui_shortcut2 as ui_shortcut2;
    use fidl_fuchsia_ui_test_scene as ui_test_scene;
    use fidl_fuchsia_ui_views as ui_views;
    use fuchsia_async as fasync;
    use fuchsia_component_test::{
        Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
    };
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

    async fn build_realm() -> anyhow::Result<RealmInstance> {
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
                    .capability(Capability::protocol_by_name("fuchsia.ui.composition.Flatland"))
                    .capability(Capability::protocol_by_name("fuchsia.ui.shortcut2.Registry"))
                    .capability(Capability::protocol_by_name("fuchsia.ui.test.input.Registry"))
                    .capability(Capability::protocol_by_name("fuchsia.ui.test.scene.Controller"))
                    .from(&test_ui_stack)
                    .to(Ref::parent()),
            )
            .await?;

        let realm = builder.build().await?;
        Ok(realm)
    }

    fn start_server_task(
        realm: &RealmInstance,
    ) -> anyhow::Result<(fasync::Task<anyhow::Result<()>>, felement::GraphicalPresenterProxy)> {
        let (graphical_presenter_proxy, graphical_presenter_request_stream) =
            endpoints::create_proxy_and_stream::<felement::GraphicalPresenterMarker>()?;

        let server_task = fasync::Task::local(test_server(
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?,
            realm.root.connect_to_protocol_at_exposed_dir::<ui_shortcut2::RegistryMarker>()?,
            realm.root.connect_to_protocol_at_exposed_dir::<ui_test_scene::ControllerMarker>()?,
            graphical_presenter_request_stream,
        ));

        Ok((server_task, graphical_presenter_proxy))
    }

    async fn test_server(
        flatland: flatland::FlatlandProxy,
        shortcut_registry: ui_shortcut2::RegistryProxy,
        scene_controller: ui_test_scene::ControllerProxy,
        mut graphical_presenter_request_stream: felement::GraphicalPresenterRequestStream,
    ) -> anyhow::Result<()> {
        let (view_provider, view_provider_request_stream) =
            endpoints::create_request_stream::<ui_app::ViewProviderMarker>()?;

        let client_view_attached = fasync::Task::spawn(async move {
            let _view_ref_koid = scene_controller
                .attach_client_view(ui_test_scene::ControllerAttachClientViewRequest {
                    view_provider: Some(view_provider),
                    ..ui_test_scene::ControllerAttachClientViewRequest::EMPTY
                })
                .await
                .expect("failed to attach root client view");
        });

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

        let mut background_results = stream::SelectAll::new();

        let mut server = Manager::new(
            View::new(flatland.clone(), view_creation_token)?,
            shortcut_registry,
            ManagerOptions {
                // Make the shortcut be "Tab", because "Alt-Tab" can't currently
                // be typed in our virtual test keyboard.
                cycle_windows_shortcut: vec![ui_input3::KeyMeaning::NonPrintableKey(
                    ui_input3::NonPrintableKey::Tab,
                )],
            },
        )
        .await?;
        let mut flatland_events = flatland.take_event_stream();

        flatland.present(flatland::PresentArgs::EMPTY)?;
        await_next_on_frame_begin(&mut flatland_events).await;

        client_view_attached.await;

        loop {
            flatland.present(flatland::PresentArgs::EMPTY)?;
            await_next_on_frame_begin(&mut flatland_events).await;

            select! {
                req = graphical_presenter_request_stream.next() => {
                    if req.is_none() {
                        break
                    }

                    let felement::GraphicalPresenterRequest::PresentView {
                        view_spec,
                        annotation_controller,
                        view_controller_request,
                        responder,
                    } = req.unwrap()?;
                    server.present_view(
                        view_spec,
                        annotation_controller,
                        view_controller_request
                    )?;
                    responder.send(&mut Ok(()))?;
                }
                bg = server.select_background_task() => {
                    bg?;
                },
                bg = background_results.select_next_some() => {
                    bg?;
                }
            }

            background_results.push(server.take_background_results());
        }
        background_results.push(server.take_background_results());

        tracing::info!("finishing background tasks...");
        flatland.present(flatland::PresentArgs::EMPTY)?;
        await_next_on_frame_begin(&mut flatland_events).await;
        background_results.for_each(|result| future::ready(result.unwrap())).await;
        Ok(())
    }

    struct TestWindow {
        _flatland: flatland::FlatlandProxy,
        _flatland_events: flatland::FlatlandEventStream,
        view_controller: felement::ViewControllerProxy,
        parent_viewport_watcher: flatland::ParentViewportWatcherProxy,
        view_ref_focused: ui_views::ViewRefFocusedProxy,
    }

    impl TestWindow {
        async fn create(
            flatland: flatland::FlatlandProxy,
            graphical_presenter_proxy: &felement::GraphicalPresenterProxy,
        ) -> anyhow::Result<Self> {
            let flatland::ViewCreationTokenPair {
                mut view_creation_token,
                viewport_creation_token,
            } = flatland::ViewCreationTokenPair::new()?;

            let (view_controller, view_controller_server) =
                endpoints::create_proxy::<felement::ViewControllerMarker>()?;

            let view_spec = felement::ViewSpec {
                viewport_creation_token: Some(viewport_creation_token),
                ..felement::ViewSpec::EMPTY
            };

            let () = graphical_presenter_proxy
                .present_view(view_spec, None, Some(view_controller_server))
                .await
                .unwrap()
                .unwrap();

            let (parent_viewport_watcher, parent_viewport_watcher_server) =
                endpoints::create_proxy::<flatland::ParentViewportWatcherMarker>()?;

            let (view_ref_focused, view_ref_focused_server) =
                endpoints::create_proxy::<fidl_fuchsia_ui_views::ViewRefFocusedMarker>()?;

            flatland.create_view2(
                &mut view_creation_token,
                &mut ui_views::ViewIdentityOnCreation::from(fuchsia_scenic::ViewRefPair::new()?),
                flatland::ViewBoundProtocols {
                    view_ref_focused: Some(view_ref_focused_server),
                    ..flatland::ViewBoundProtocols::EMPTY
                },
                parent_viewport_watcher_server,
            )?;

            let mut flatland_events = flatland.take_event_stream();

            flatland.present(flatland::PresentArgs::EMPTY)?;
            await_next_on_frame_begin(&mut flatland_events).await;

            Ok(Self {
                _flatland: flatland,
                _flatland_events: flatland_events,
                view_controller,
                parent_viewport_watcher,
                view_ref_focused,
            })
        }

        async fn until_status_is(&self, status: ui_comp::ParentViewportStatus) {
            until_true_or_timeout(
                || async { self.parent_viewport_watcher.get_status().await.unwrap() == status },
                fasync::Duration::from_seconds(10),
            )
            .await

            // while self.parent_viewport_watcher.get_status().await.unwrap() != status {}
        }

        async fn until_focused_is(&self, focused: bool) {
            until_true_or_timeout(
                || async {
                    self.view_ref_focused.watch().await.unwrap().focused.unwrap() == focused
                },
                fasync::Duration::from_seconds(10),
            )
            .await
        }
    }

    /// Helper function that continuously polls `pred` until it returns true or
    /// the timeout elapses.
    async fn until_true_or_timeout<Pred, Fut>(mut pred: Pred, timeout: fasync::Duration)
    where
        Pred: FnMut() -> Fut,
        Fut: future::Future<Output = bool>,
    {
        let mut timer = fasync::Timer::new(fasync::Time::after(timeout)).fuse();

        loop {
            select! {
                result = pred().fuse() => if result {
                    return
                },
                _ = timer => panic!("deadline exceeded")
            }
        }
    }

    #[fuchsia::test]
    async fn no_windows() -> Result<(), Error> {
        let realm = build_realm().await?;

        let (server_task, graphical_presenter_proxy) = start_server_task(&realm)?;

        // Delete the `GraphicalPresenterProxy`, which tells the server to shut
        // down.
        std::mem::drop(graphical_presenter_proxy);
        server_task.await?;
        realm.destroy().await?;
        Ok(())
    }

    #[fuchsia::test]
    async fn create_and_dismiss() -> Result<(), Error> {
        let realm = build_realm().await?;
        let (server_task, graphical_presenter_proxy) = start_server_task(&realm)?;

        // Create a window.
        let window = TestWindow::create(
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?,
            &graphical_presenter_proxy,
        )
        .await?;

        // Wait for the child to be attached and focused.
        window.until_status_is(ui_comp::ParentViewportStatus::ConnectedToDisplay).await;
        window.until_focused_is(true).await;

        // Dismiss the view, and wait for the parent_viewport_watcher to close.
        window.view_controller.dismiss()?;
        window.parent_viewport_watcher.on_closed().await?;

        // Delete the `GraphicalPresenterProxy`, which tells the server to shut
        // down.
        std::mem::drop(graphical_presenter_proxy);
        server_task.await?;
        realm.destroy().await?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_active_window_stack() -> Result<(), Error> {
        let realm = build_realm().await?;
        let (server_task, graphical_presenter_proxy) = start_server_task(&realm)?;

        // Create a window.
        let window1 = TestWindow::create(
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?,
            &graphical_presenter_proxy,
        )
        .await?;

        // Wait for the child to be attached and focused.
        window1.until_status_is(ui_comp::ParentViewportStatus::ConnectedToDisplay).await;
        window1.until_focused_is(true).await;

        // Create a second window. It should get focus, etc.
        let window2 = TestWindow::create(
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?,
            &graphical_presenter_proxy,
        )
        .await?;
        window2.until_status_is(ui_comp::ParentViewportStatus::ConnectedToDisplay).await;
        window2.until_focused_is(true).await;

        // Check that window1 has lost focus and is detached.
        window1.until_status_is(ui_comp::ParentViewportStatus::DisconnectedFromDisplay).await;
        window1.until_focused_is(false).await;

        // Dismiss window2.
        window2.view_controller.dismiss()?;
        window2.parent_viewport_watcher.on_closed().await?;

        // window1 should reattach and gain focus.
        window1.until_status_is(ui_comp::ParentViewportStatus::ConnectedToDisplay).await;
        window1.until_focused_is(true).await;

        // Dismiss the view, and wait for the parent_viewport_watcher to close.
        window1.view_controller.dismiss()?;
        window1.parent_viewport_watcher.on_closed().await?;

        // Delete the `GraphicalPresenterProxy`, which tells the server to shut
        // down.
        std::mem::drop(graphical_presenter_proxy);
        server_task.await?;
        realm.destroy().await?;
        Ok(())
    }

    #[fuchsia::test]
    async fn test_dismiss_background_window() -> Result<(), Error> {
        let realm = build_realm().await?;
        let (server_task, graphical_presenter_proxy) = start_server_task(&realm)?;

        // Create a window.
        let window1 = TestWindow::create(
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?,
            &graphical_presenter_proxy,
        )
        .await?;

        // Wait for the child to be attached and focused.
        window1.until_status_is(ui_comp::ParentViewportStatus::ConnectedToDisplay).await;
        window1.until_focused_is(true).await;

        // Create a second window.
        let window2 = TestWindow::create(
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?,
            &graphical_presenter_proxy,
        )
        .await?;
        window2.until_status_is(ui_comp::ParentViewportStatus::ConnectedToDisplay).await;
        window2.until_focused_is(true).await;

        // Dismiss window1. We just want to make sure nothing crashes.
        window1.view_controller.dismiss()?;
        window1.parent_viewport_watcher.on_closed().await?;

        // Dismiss window2.
        window2.view_controller.dismiss()?;
        window2.parent_viewport_watcher.on_closed().await?;

        // Delete the `GraphicalPresenterProxy`, which tells the server to shut
        // down.
        std::mem::drop(graphical_presenter_proxy);
        server_task.await?;
        realm.destroy().await?;
        Ok(())
    }

    #[fuchsia::test]
    async fn immediately_close() -> Result<(), Error> {
        let realm = build_realm().await?;
        let (server_task, graphical_presenter_proxy) = start_server_task(&realm)?;

        // Call present_view with a viewport_creation_token whose
        // view_creation_token is already closed.
        let flatland::ViewCreationTokenPair { view_creation_token: _, viewport_creation_token } =
            flatland::ViewCreationTokenPair::new()?;

        let (_view_controller, view_controller_server) =
            endpoints::create_proxy::<felement::ViewControllerMarker>()?;

        let () = graphical_presenter_proxy
            .present_view(
                felement::ViewSpec {
                    viewport_creation_token: Some(viewport_creation_token),
                    ..felement::ViewSpec::EMPTY
                },
                None,
                Some(view_controller_server),
            )
            .await
            .unwrap()
            .unwrap();

        std::mem::drop(graphical_presenter_proxy);

        server_task.await?;
        realm.destroy().await?;
        Ok(())
    }

    #[fuchsia::test]
    async fn dismiss_before_attach() -> Result<(), Error> {
        let realm = build_realm().await?;
        let (server_task, graphical_presenter_proxy) = start_server_task(&realm)?;

        let flatland::ViewCreationTokenPair { mut view_creation_token, viewport_creation_token } =
            flatland::ViewCreationTokenPair::new()?;

        let (view_controller, view_controller_server) =
            endpoints::create_proxy::<felement::ViewControllerMarker>()?;

        let () = graphical_presenter_proxy
            .present_view(
                felement::ViewSpec {
                    viewport_creation_token: Some(viewport_creation_token),
                    ..felement::ViewSpec::EMPTY
                },
                None,
                Some(view_controller_server),
            )
            .await
            .unwrap()
            .unwrap();

        view_controller.dismiss()?;

        let (_parent_viewport_watcher, parent_viewport_watcher_request) =
            endpoints::create_proxy::<flatland::ParentViewportWatcherMarker>()?;

        let (_view_ref_focused, view_ref_focused_server) =
            endpoints::create_proxy::<fidl_fuchsia_ui_views::ViewRefFocusedMarker>()?;

        let flatland =
            realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?;
        flatland.create_view2(
            &mut view_creation_token,
            &mut ui_views::ViewIdentityOnCreation::from(fuchsia_scenic::ViewRefPair::new()?),
            flatland::ViewBoundProtocols {
                view_ref_focused: Some(view_ref_focused_server),
                ..flatland::ViewBoundProtocols::EMPTY
            },
            parent_viewport_watcher_request,
        )?;

        std::mem::drop(graphical_presenter_proxy);

        server_task.await?;
        realm.destroy().await?;
        Ok(())
    }

    // TODO(fxbug.dev/113350): Deflake this.
    // #[fuchsia::test]
    // async fn cycle_windows() -> Result<(), Error> {
    //     let realm = build_realm().await?;
    //     let (server_task, graphical_presenter_proxy) = start_server_task(&realm)?;

    //     let (keyboard, keyboard_server) =
    //         endpoints::create_proxy::<ui_test_input::KeyboardMarker>()?;
    //     let input_registry =
    //         realm.root.connect_to_protocol_at_exposed_dir::<ui_test_input::RegistryMarker>()?;

    //     input_registry
    //         .register_keyboard(ui_test_input::RegistryRegisterKeyboardRequest {
    //             device: Some(keyboard_server),
    //             ..ui_test_input::RegistryRegisterKeyboardRequest::EMPTY
    //         })
    //         .await?;

    //     // Create three windows.
    //     let window1 = TestWindow::create(
    //         realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?,
    //         &graphical_presenter_proxy,
    //     )
    //     .await?;
    //     let window2 = TestWindow::create(
    //         realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?,
    //         &graphical_presenter_proxy,
    //     )
    //     .await?;
    //     let window3 = TestWindow::create(
    //         realm.root.connect_to_protocol_at_exposed_dir::<flatland::FlatlandMarker>()?,
    //         &graphical_presenter_proxy,
    //     )
    //     .await?;

    //     window3.until_status_is(ui_comp::ParentViewportStatus::ConnectedToDisplay).await;
    //     window3.until_focused_is(true).await;

    //     let send_text = |t: String| async {
    //         keyboard
    //             .simulate_us_ascii_text_entry(
    //                 ui_test_input::KeyboardSimulateUsAsciiTextEntryRequest {
    //                     text: Some(t),
    //                     ..ui_test_input::KeyboardSimulateUsAsciiTextEntryRequest::EMPTY
    //                 },
    //             )
    //             .await
    //             .expect("Failed to inject text using fuchsia.ui.test.input.Keyboard");
    //     };

    //     send_text("\t".to_string()).await;

    //     window2.until_status_is(ui_comp::ParentViewportStatus::ConnectedToDisplay).await;
    //     window2.until_focused_is(true).await;
    //     window3.until_status_is(ui_comp::ParentViewportStatus::DisconnectedFromDisplay).await;
    //     window3.until_focused_is(false).await;

    //     send_text("\t".to_string()).await;

    //     window1.until_status_is(ui_comp::ParentViewportStatus::ConnectedToDisplay).await;
    //     window1.until_focused_is(true).await;
    //     window2.until_status_is(ui_comp::ParentViewportStatus::DisconnectedFromDisplay).await;
    //     window2.until_focused_is(false).await;

    //     send_text("\t".to_string()).await;

    //     window3.until_status_is(ui_comp::ParentViewportStatus::ConnectedToDisplay).await;
    //     window3.until_focused_is(true).await;
    //     window1.until_status_is(ui_comp::ParentViewportStatus::DisconnectedFromDisplay).await;
    //     window1.until_focused_is(false).await;

    //     // Delete the `GraphicalPresenterProxy`, which tells the server to shut
    //     // down.
    //     std::mem::drop(graphical_presenter_proxy);
    //     server_task.await?;
    //     realm.destroy().await?;
    //     Ok(())
    // }
}
