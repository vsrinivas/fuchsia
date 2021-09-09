// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::{Client, TaskQueue},
    crate::compositor::{Surface, SurfaceCommand, SurfaceRole},
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    anyhow::{format_err, Error},
    fidl::endpoints::{create_endpoints, RequestStream},
    fidl_fuchsia_math::{Rect, Size, SizeF},
    fidl_fuchsia_ui_app::{ViewProviderControlHandle, ViewProviderMarker, ViewProviderRequest},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_trace as ftrace, fuchsia_wayland_core as wl,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
    zxdg_shell_v6::{
        zxdg_toplevel_v6, ZxdgPopupV6, ZxdgPopupV6Event, ZxdgPopupV6Request, ZxdgPositionerV6,
        ZxdgPositionerV6Request, ZxdgShellV6, ZxdgShellV6Request, ZxdgSurfaceV6,
        ZxdgSurfaceV6Event, ZxdgSurfaceV6Request, ZxdgToplevelV6, ZxdgToplevelV6Event,
        ZxdgToplevelV6Request,
    },
};

#[cfg(feature = "flatland")]
use {
    crate::scenic::Flatland,
    crate::Callback,
    async_utils::hanging_get::client::HangingGetStream,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_ui_composition::{
        self as composition, FlatlandEvent, FlatlandEventStream, ParentViewportWatcherProxy,
    },
};

#[cfg(not(feature = "flatland"))]
use {
    crate::scenic::ScenicSession,
    fidl::endpoints::{create_request_stream, ServerEnd},
    fidl_fuchsia_ui_gfx::{self as gfx, ColorRgba},
    fidl_fuchsia_ui_scenic::{
        SessionListenerControlHandle, SessionListenerMarker, SessionListenerRequest,
    },
    fidl_fuchsia_ui_views::{ViewRef, ViewToken},
    fuchsia_scenic::{EntityNode, Material, Rectangle, ShapeNode, View},
};

static NEXT_VIEW_ID: AtomicUsize = AtomicUsize::new(1);

/// `XdgShell` is an implementation of the zxdg_shell_v6 global.
///
/// `XdgShell` is used to create traditional desktop-style applications. The
/// `XdgShell` can be used to create `XdgSurface` objects. Similar to `Surface`,
/// an `XdgSurface` doesn't do much good on it's own until it's assigned a
/// sub-role (ex: `XdgToplevel`, `XdgPopup`).
pub struct XdgShell;

impl XdgShell {
    /// Creates a new `XdgShell` global.
    pub fn new() -> Self {
        Self
    }
}

impl RequestReceiver<ZxdgShellV6> for XdgShell {
    fn receive(
        this: ObjectRef<Self>,
        request: ZxdgShellV6Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZxdgShellV6Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZxdgShellV6Request::GetXdgSurface { id, surface } => {
                let xdg_surface = XdgSurface::new(surface);
                let surface_ref = xdg_surface.surface_ref;
                let xdg_surface_ref = id.implement(client, xdg_surface)?;
                surface_ref.get_mut(client)?.set_role(SurfaceRole::XdgSurface(xdg_surface_ref))?;
            }
            ZxdgShellV6Request::CreatePositioner { id } => {
                id.implement(client, XdgPositioner)?;
            }
            ZxdgShellV6Request::Pong { .. } => {}
        }
        Ok(())
    }
}

struct XdgPositioner;

impl RequestReceiver<ZxdgPositionerV6> for XdgPositioner {
    fn receive(
        this: ObjectRef<Self>,
        request: ZxdgPositionerV6Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZxdgPositionerV6Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZxdgPositionerV6Request::SetSize { .. } => {}
            ZxdgPositionerV6Request::SetAnchorRect { .. } => {}
            ZxdgPositionerV6Request::SetAnchor { .. } => {}
            ZxdgPositionerV6Request::SetGravity { .. } => {}
            ZxdgPositionerV6Request::SetConstraintAdjustment { .. } => {}
            ZxdgPositionerV6Request::SetOffset { .. } => {}
        }
        Ok(())
    }
}

/// An `XdgSurface` is the common base to the different surfaces in the
/// `XdgShell` (ex: `XdgToplevel`, `XdgPopup`).
pub struct XdgSurface {
    /// A reference to the underlying `Surface` for this `XdgSurface`.
    surface_ref: ObjectRef<Surface>,
    /// The sub-role assigned to this `XdgSurface`. This is needed because the
    /// `XdgSurface` is not a role itself, but a base for the concrete XDG
    /// surface roles.
    xdg_role: Option<XdgSurfaceRole>,
}

impl XdgSurface {
    /// Creates a new `XdgSurface`.
    pub fn new(id: wl::ObjectId) -> Self {
        XdgSurface { surface_ref: id.into(), xdg_role: None }
    }

    /// Sets the concrete role for this `XdgSurface`.
    ///
    /// Similar to `Surface`, an `XdgSurface` isn't of much use until a role
    /// has been assigned.
    pub fn set_xdg_role(&mut self, xdg_role: XdgSurfaceRole) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurface::set_xdg_role");
        if let Some(current_role) = &self.xdg_role {
            Err(format_err!(
                "Attemping to re-assign xdg_surface role {:?} to {:?}",
                current_role,
                xdg_role
            ))
        } else {
            self.xdg_role = Some(xdg_role);
            Ok(())
        }
    }

    /// Returns a reference to the underlying `Surface` for this `XdgSurface`.
    pub fn surface_ref(&self) -> ObjectRef<Surface> {
        self.surface_ref
    }

    /// Concludes a surface configuration sequence.
    ///
    /// Each concrete `XdgSurface` role configuration sequence is concluded and
    /// committed by a xdg_surface::configure event.
    pub fn configure(this: ObjectRef<Self>, client: &Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurface::configure");
        let serial = client.event_queue().next_serial();
        client.event_queue().post(this.id(), ZxdgSurfaceV6Event::Configure { serial })?;
        Ok(())
    }

    /// Handle a commit request to this `XdgSurface`.
    ///
    /// This will be triggered by a wl_surface::commit request to the backing
    /// wl_surface object for this xdg_surface, and simply delegates the request
    /// to the concrete surface.
    pub fn finalize_commit(this: &ObjectRef<Self>, client: &mut Client) -> Result<bool, Error> {
        ftrace::duration!("wayland", "XdgSurface::finalize_commit");
        let xdg_surface = this.get(client)?;
        match xdg_surface.xdg_role {
            Some(XdgSurfaceRole::Toplevel(toplevel)) => {
                XdgToplevel::finalize_commit(toplevel, client)
            }
            _ => Ok(false),
        }
    }
}

impl RequestReceiver<ZxdgSurfaceV6> for XdgSurface {
    fn receive(
        this: ObjectRef<Self>,
        request: ZxdgSurfaceV6Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZxdgSurfaceV6Request::Destroy => {
                client.delete_id(this.id())?;
            }
            #[cfg(not(feature = "flatland"))]
            ZxdgSurfaceV6Request::GetToplevel { id } => {
                let (client_end, server_end) = create_endpoints::<SessionListenerMarker>().unwrap();
                let session = client.display().create_session(Some(client_end))?;
                let toplevel = XdgToplevel::new(this, client, session.clone())?;
                let toplevel_ref = id.implement(client, toplevel)?;
                client.xdg_toplevels.add(toplevel_ref);
                this.get_mut(client)?.set_xdg_role(XdgSurfaceRole::Toplevel(toplevel_ref))?;
                let view_provider_control_handle =
                    XdgToplevel::spawn_view_provider(toplevel_ref, client, session)?;
                let session_listener_control_handle =
                    XdgToplevel::spawn_session_listener(toplevel_ref, client, server_end)?;
                let toplevel = toplevel_ref.get_mut(client)?;
                toplevel.view_provider_controller = Some(view_provider_control_handle);
                toplevel.session_listener_controller = Some(session_listener_control_handle);
            }
            #[cfg(feature = "flatland")]
            ZxdgSurfaceV6Request::GetToplevel { id } => {
                let proxy = connect_to_protocol::<composition::FlatlandMarker>()
                    .expect("error connecting to Flatland");
                let flatland = Flatland::new(proxy);
                let toplevel = XdgToplevel::new(this, client, flatland.clone())?;
                let toplevel_ref = id.implement(client, toplevel)?;
                client.xdg_toplevels.add(toplevel_ref);
                this.get_mut(client)?.set_xdg_role(XdgSurfaceRole::Toplevel(toplevel_ref))?;
                let view_provider_control_handle =
                    XdgToplevel::spawn_view_provider(toplevel_ref, client, flatland.clone())?;
                XdgToplevel::spawn_flatland_listener(
                    toplevel_ref,
                    client,
                    flatland.proxy().take_event_stream(),
                )?;
                let toplevel = toplevel_ref.get_mut(client)?;
                toplevel.view_provider_controller = Some(view_provider_control_handle);
            }
            ZxdgSurfaceV6Request::GetPopup { id, parent, positioner } => {
                let popup_ref =
                    id.implement(client, XdgPopup::new(parent.into(), positioner.into()))?;

                // TODO: We don't yet draw the popups, so for now we will
                // immediately send the 'done' event to let the client know the
                // popup has been dismissed.
                client.event_queue().post(popup_ref.id(), ZxdgPopupV6Event::PopupDone)?;
            }
            ZxdgSurfaceV6Request::SetWindowGeometry { x, y, width, height } => {
                let surface_ref = this.get(client)?.surface_ref;
                surface_ref.get_mut(client)?.enqueue(SurfaceCommand::SetWindowGeometry(Rect {
                    x,
                    y,
                    width,
                    height,
                }));
            }
            ZxdgSurfaceV6Request::AckConfigure { .. } => {}
        }
        Ok(())
    }
}

/// Models the different roles that can be assigned to an `XdgSurface`.
#[derive(Copy, Clone, Debug)]
pub enum XdgSurfaceRole {
    Toplevel(ObjectRef<XdgToplevel>),
}

struct XdgPopup {
    // These will be wired up in a subsequent change.
    #[allow(dead_code)]
    parent: ObjectRef<XdgSurface>,
    #[allow(dead_code)]
    positioner: ObjectRef<XdgPositioner>,
}

impl XdgPopup {
    pub fn new(parent: ObjectRef<XdgSurface>, positioner: ObjectRef<XdgPositioner>) -> Self {
        XdgPopup { parent, positioner }
    }
}

impl RequestReceiver<ZxdgPopupV6> for XdgPopup {
    fn receive(
        this: ObjectRef<Self>,
        request: ZxdgPopupV6Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZxdgPopupV6Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZxdgPopupV6Request::Grab { .. } => {}
        }
        Ok(())
    }
}

/// Physical size used for configure events prior to receiving
/// layout information from the view system.
const DEFAULT_PHYSICAL_SIZE: Size = Size { width: 256, height: 256 };

/// `XdgToplevel` is a surface that should appear as a top-level window.
///
/// `XdgToplevel` will be implemented as a scenic `View`/`ViewProvider` that
/// hosts the surface contents. The actual presentation of the `View` will be
/// deferred to whatever user shell is used.
pub struct XdgToplevel {
    /// A reference to the underlying wl_surface for this toplevel.
    surface_ref: ObjectRef<Surface>,
    /// A reference to the underlying xdg_surface for this toplevel.
    xdg_surface_ref: ObjectRef<XdgSurface>,
    /// The associated scenic view for this toplevel. This will be populated
    /// in response to requests to the public `ViewProvider` service.
    view: Option<XdgToplevelViewPtr>,
    /// This handle can be used to terminate the |ViewProvider| FIDL service
    /// associated with this toplevel.
    view_provider_controller: Option<ViewProviderControlHandle>,
    /// This handle can be used to terminate the |SessionListener| FIDL service
    /// associated with this toplevel.
    #[cfg(not(feature = "flatland"))]
    session_listener_controller: Option<SessionListenerControlHandle>,
    /// Identifier for the view.
    view_id: u32,
    /// This will be set to false after we received an initial commit.
    waiting_for_initial_commit: bool,
}

impl XdgToplevel {
    /// Sets the backing view for this toplevel.
    fn set_view(&mut self, view: XdgToplevelViewPtr) {
        // We shut down the ViewProvider after creating the first view, so this
        // should never happen.
        assert!(self.view.is_none());
        self.view = Some(view);
    }

    /// Performs a configure sequence for the XdgToplevel object referenced by
    /// `this`.
    pub fn configure(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgToplevel::configure");
        let (width, height, xdg_surface_ref, surface_ref) = {
            let (view, xdg_surface_ref, surface_ref) = {
                let toplevel = this.get(client)?;
                (toplevel.view.clone(), toplevel.xdg_surface_ref, toplevel.surface_ref)
            };
            let physical_size = view
                .as_ref()
                .map(|view| view.lock().physical_size(client))
                .unwrap_or(DEFAULT_PHYSICAL_SIZE);
            (physical_size.width, physical_size.height, xdg_surface_ref, surface_ref)
        };

        // Always set the fullscreen state to hint to the client it really should
        // obey the geometry we're asking. From the xdg_shell spec:
        //
        // fullscreen:
        //    The surface is fullscreen. The window geometry specified in the
        //    configure event must be obeyed by the client.
        let mut states = wl::Array::new();
        states.push(zxdg_toplevel_v6::State::Fullscreen)?;
        if client.input_dispatcher.has_focus(surface_ref) {
            // If the window has focus, we set the activiated state. This is
            // just a hint to pass along to the client so it can draw itself
            // differently with and without focus.
            states.push(zxdg_toplevel_v6::State::Activated)?;
        }
        client
            .event_queue()
            .post(this.id(), ZxdgToplevelV6Event::Configure { width, height, states })?;

        // Send the xdg_surface::configure event to mark the end of the
        // configuration sequence.
        XdgSurface::configure(xdg_surface_ref, client)?;
        Ok(())
    }

    pub fn finalize_commit(this: ObjectRef<Self>, client: &mut Client) -> Result<bool, Error> {
        ftrace::duration!("wayland", "XdgToplevel::finalize_commit");
        let top_level = this.get_mut(client)?;
        // Initial commit requires that we send a configure event.
        if top_level.waiting_for_initial_commit {
            top_level.waiting_for_initial_commit = false;
            Self::configure(this, client)?;
        }
        Ok(true)
    }
}

#[cfg(feature = "flatland")]
impl XdgToplevel {
    /// Creates a new `XdgToplevel` surface.
    pub fn new(
        xdg_surface_ref: ObjectRef<XdgSurface>,
        client: &mut Client,
        flatland: Flatland,
    ) -> Result<Self, Error> {
        let surface_ref = xdg_surface_ref.get(client)?.surface_ref();
        surface_ref.get_mut(client)?.set_flatland(flatland)?;
        Ok(XdgToplevel {
            surface_ref,
            xdg_surface_ref,
            view: None,
            view_provider_controller: None,
            view_id: NEXT_VIEW_ID.fetch_add(1, Ordering::SeqCst) as u32,
            waiting_for_initial_commit: true,
        })
    }

    fn spawn_flatland_listener(
        this: ObjectRef<Self>,
        client: &mut Client,
        stream: FlatlandEventStream,
    ) -> Result<(), Error> {
        let task_queue = client.task_queue();
        let surface_ref = this.get(client)?.surface_ref;
        fasync::Task::local(
            stream
                .try_for_each(move |event| {
                    match event {
                        FlatlandEvent::OnNextFrameBegin { values } => {
                            task_queue.post(move |client| {
                                let infos = values
                                    .future_presentation_infos
                                    .as_ref()
                                    .expect("no future presentation infos");
                                let info =
                                    infos.iter().next().expect("no future presentation info");
                                let time_ms =
                                    (info.presentation_time.expect("no presentation time")
                                        / 1_000_000) as u32;
                                // TODO: Remove this check when OnNextFrameBegin is only sent as a
                                // result of Present.
                                if let Some(callbacks) =
                                    surface_ref.get_mut(client)?.next_callbacks()
                                {
                                    callbacks.iter().try_for_each(|callback| {
                                        Callback::done(*callback, client, time_ms)?;
                                        client.delete_id(callback.id())
                                    })?;
                                }
                                surface_ref.get_mut(client)?.add_present_credits(
                                    values.additional_present_credits.unwrap_or(0),
                                );
                                Ok(())
                            });
                        }
                        FlatlandEvent::OnFramePresented { frame_presented_info: _ } => {}
                        FlatlandEvent::OnError { error } => {
                            println!("FlatlandEvent::OnError: {:?}", error);
                        }
                    };
                    future::ok(())
                })
                .unwrap_or_else(|e| eprintln!("error listening for Flatland Events: {:?}", e)),
        )
        .detach();
        Ok(())
    }

    fn spawn_parent_viewport_listener(
        this: ObjectRef<Self>,
        parent_viewport_watcher: ParentViewportWatcherProxy,
        task_queue: TaskQueue,
    ) {
        let mut layout_info_stream =
            HangingGetStream::new(Box::new(move || Some(parent_viewport_watcher.get_layout())));

        fasync::Task::local(async move {
            while let Some(result) = layout_info_stream.next().await {
                match result {
                    Ok(layout_info) => {
                        let logical_size = layout_info
                            .logical_size
                            .map(|size| SizeF {
                                width: size.width as f32,
                                height: size.height as f32,
                            })
                            .expect("layout info is missing logical size");
                        task_queue.post(move |client| {
                            if let Some(view) = this.get_mut(client)?.view.clone() {
                                view.lock().handle_layout_changed(&logical_size);
                            }
                            Ok(())
                        });
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
                        task_queue.post(|_client| {
                            // Returning an error causes the client connection to be
                            // closed (and that typically closes the application).
                            Err(format_err!("Parent viewport watcher channel closed"))
                        });
                        return;
                    }
                    Err(fidl_error) => {
                        println!("graph link GetLayout() error: {:?}", fidl_error);
                        return;
                    }
                }
            }
        })
        .detach();
    }

    fn spawn_view_provider(
        this: ObjectRef<Self>,
        client: &mut Client,
        flatland: Flatland,
    ) -> Result<ViewProviderControlHandle, Error> {
        ftrace::duration!("wayland", "XdgToplevel::spawn_view_provider");
        // Create a new ViewProvider service, hand off the client endpoint to
        // our ViewSink to be presented.
        let (client_end, server_end) = create_endpoints::<ViewProviderMarker>().unwrap();
        let view_id = this.get(client)?.view_id;
        client.display().new_view_provider(client_end, view_id);

        // Spawn the view provider server for this surface.
        let surface_ref = this.get(client)?.surface_ref;
        let task_queue = client.task_queue();
        let mut stream = server_end.into_stream().unwrap();
        let control_handle = stream.control_handle();
        fasync::Task::local(
            async move {
                while let Some(request) = stream.try_next().await.unwrap() {
                    match request {
                        ViewProviderRequest::CreateView2 { args, .. } => {
                            let mut view_creation_token = args.view_creation_token.unwrap();
                            let (parent_viewport_watcher, server_end) =
                                create_proxy::<composition::ParentViewportWatcherMarker>()
                                    .expect("failed to create ParentViewportWatcherProxy");
                            flatland
                                .proxy()
                                .create_view(&mut view_creation_token, server_end)
                                .expect("fidl error");
                            XdgToplevel::spawn_parent_viewport_listener(
                                this,
                                parent_viewport_watcher,
                                task_queue.clone(),
                            );
                            let view_ptr = XdgToplevelView::new(
                                flatland,
                                task_queue.clone(),
                                this,
                                surface_ref,
                            )?;
                            {
                                let view_ptr = view_ptr.clone();
                                task_queue.post(move |client| {
                                    let surface_ref = this.get(client)?.surface_ref;
                                    view_ptr.lock().attach(surface_ref, client);
                                    this.get_mut(client)?.set_view(view_ptr.clone());
                                    Ok(())
                                });
                            }
                        }
                        _ => {
                            panic!("unsupported view provider request: {:?}", request)
                        }
                    }
                    // We only support a single view, so we'll stop handling
                    // CreateView requests after we create the first view.
                    while let Some(request) = stream.try_next().await.unwrap() {
                        panic!("unsupported view provider request: {:?}", request)
                    }
                    break;
                }
                task_queue.post(|_client| {
                    // Returning an error causes the client connection to be
                    // closed (and that typically closes the application).
                    Err(format_err!("View provider channel closed "))
                });
                Ok(())
            }
            .unwrap_or_else(|e: Error| println!("{:?}", e)),
        )
        .detach();
        Ok(control_handle)
    }

    pub fn shutdown(&self, client: &Client) {
        self.view_provider_controller.as_ref().map(|h| h.shutdown());
        self.view.as_ref().map(|v| v.lock().shutdown());
        client.display().delete_view_provider(self.view_id);
    }
}

#[cfg(not(feature = "flatland"))]
impl XdgToplevel {
    /// Creates a new `XdgToplevel` surface.
    pub fn new(
        xdg_surface_ref: ObjectRef<XdgSurface>,
        client: &mut Client,
        session: ScenicSession,
    ) -> Result<Self, Error> {
        let surface_ref = xdg_surface_ref.get(client)?.surface_ref();
        surface_ref.get_mut(client)?.set_session(session)?;
        Ok(XdgToplevel {
            surface_ref,
            xdg_surface_ref,
            view: None,
            view_provider_controller: None,
            session_listener_controller: None,
            view_id: NEXT_VIEW_ID.fetch_add(1, Ordering::SeqCst) as u32,
            waiting_for_initial_commit: true,
        })
    }

    fn handle_gfx_event(
        toplevel_ref: ObjectRef<Self>,
        event: fidl_fuchsia_ui_gfx::Event,
        task_queue: &TaskQueue,
    ) {
        ftrace::duration!("wayland", "XdgToplevel::handle_gfx_event");
        match event {
            fidl_fuchsia_ui_gfx::Event::Metrics(fidl_fuchsia_ui_gfx::MetricsEvent {
                metrics: e,
                node_id: _,
            }) => task_queue.post(move |client| {
                if let Some(view) = toplevel_ref.get_mut(client)?.view.clone() {
                    view.lock().set_pixel_scale(e.scale_x, e.scale_y);
                }
                Ok(())
            }),
            fidl_fuchsia_ui_gfx::Event::ViewPropertiesChanged(
                fidl_fuchsia_ui_gfx::ViewPropertiesChangedEvent { properties, .. },
            ) => task_queue.post(move |client| {
                if let Some(view) = toplevel_ref.get_mut(client)?.view.clone() {
                    view.lock().handle_properies_changed(&properties);
                }
                Ok(())
            }),

            e => println!("Got unhandled gfx event: {:?}", e),
        }
    }

    fn handle_input_events(
        toplevel_ref: ObjectRef<Self>,
        surface_ref: ObjectRef<Surface>,
        events: Vec<fidl_fuchsia_ui_input::InputEvent>,
        task_queue: &TaskQueue,
    ) {
        task_queue.post(move |client| {
            ftrace::duration!("wayland", "XdgToplevel::handle_input_events");
            let had_focus = client.input_dispatcher.has_focus(surface_ref);
            // If the client has set window geometry we'll place the scenic
            // surface at the (x,y) location specified in the window geometry.
            //
            // To compenstate for this, we need to apply a translation to the
            // pointer events received by scenic to adjust for this.
            let (pointer_translation, pixel_scale) = {
                let surface = surface_ref.get(client)?;
                let geometry = surface.window_geometry();
                let translation = (geometry.x, geometry.y);
                let pixel_scale = surface.pixel_scale();
                (translation, pixel_scale)
            };
            client.input_dispatcher.handle_input_events(
                surface_ref,
                &events,
                pointer_translation,
                pixel_scale,
            )?;

            let has_focus = client.input_dispatcher.has_focus(surface_ref);
            if had_focus != has_focus {
                // If our focus has changed we need to reconfigure so that the
                // Activated flag can be set or cleared.
                XdgToplevel::configure(toplevel_ref, client)?;
            }
            Ok(())
        });
    }

    fn handle_session_events(
        toplevel_ref: ObjectRef<Self>,
        surface_ref: ObjectRef<Surface>,
        events: Vec<fidl_fuchsia_ui_scenic::Event>,
        task_queue: TaskQueue,
    ) {
        ftrace::duration!("wayland", "XdgToplevel::handle_session_events");
        let mut input_events = Vec::new();
        for event in events.into_iter() {
            match event {
                fidl_fuchsia_ui_scenic::Event::Input(e) => input_events.push(e),
                fidl_fuchsia_ui_scenic::Event::Gfx(e) => {
                    Self::handle_gfx_event(toplevel_ref, e, &task_queue)
                }
                fidl_fuchsia_ui_scenic::Event::Unhandled(c) => {
                    assert!(false, "Unhandled command {:?}", c)
                }
            }
        }

        if !input_events.is_empty() {
            Self::handle_input_events(toplevel_ref, surface_ref, input_events, &task_queue);
        }
    }

    fn spawn_session_listener(
        this: ObjectRef<Self>,
        client: &mut Client,
        server_end: ServerEnd<SessionListenerMarker>,
    ) -> Result<SessionListenerControlHandle, Error> {
        let task_queue = client.task_queue();
        let surface_ref = this.get(client)?.surface_ref;
        let mut stream = server_end.into_stream().unwrap();
        let control_handle = stream.control_handle();
        fasync::Task::local(
            async move {
                while let Some(request) = stream.try_next().await.unwrap() {
                    match request {
                        SessionListenerRequest::OnScenicError { error, .. } => {
                            println!("Scenic error! {}", error);
                        }
                        SessionListenerRequest::OnScenicEvent { events, .. } => {
                            Self::handle_session_events(
                                this,
                                surface_ref,
                                events,
                                task_queue.clone(),
                            );
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| println!("{:?}", e)),
        )
        .detach();
        Ok(control_handle)
    }

    fn spawn_view_provider(
        this: ObjectRef<Self>,
        client: &mut Client,
        session: ScenicSession,
    ) -> Result<ViewProviderControlHandle, Error> {
        ftrace::duration!("wayland", "XdgToplevel::spawn_view_provider");
        // Create a new ViewProvider service, hand off the client endpoint to
        // our ViewSink to be presented.
        let (client_end, server_end) = create_endpoints::<ViewProviderMarker>().unwrap();
        let view_id = this.get(client)?.view_id;
        client.display().new_view_provider(client_end, view_id);

        // Spawn the view provider server for this surface.
        let surface_ref = this.get(client)?.surface_ref;
        let task_queue = client.task_queue();
        let mut stream = server_end.into_stream().unwrap();
        let control_handle = stream.control_handle();
        fasync::Task::local(
            async move {
                while let Some(request) = stream.try_next().await.unwrap() {
                    match request {
                        ViewProviderRequest::CreateViewWithViewRef {
                            token,
                            view_ref_control,
                            view_ref,
                            ..
                        } => {
                            let view_token = ViewToken { value: token };
                            let view = View::new3(
                                session.as_inner().clone(),
                                view_token,
                                view_ref_control,
                                fuchsia_scenic::duplicate_view_ref(&view_ref)?,
                                Some(String::from("Wayland View")),
                            );
                            XdgToplevel::spawn_keyboard_listener(
                                this,
                                surface_ref,
                                view_ref,
                                task_queue.clone(),
                            )?;
                            let view_ptr = XdgToplevelView::new(
                                view,
                                session,
                                task_queue.clone(),
                                this,
                                surface_ref,
                            )?;
                            {
                                let view_ptr = view_ptr.clone();
                                task_queue.post(move |client| {
                                    let surface_ref = this.get(client)?.surface_ref;
                                    view_ptr.lock().attach(surface_ref, client);
                                    this.get_mut(client)?.set_view(view_ptr.clone());
                                    Ok(())
                                });
                            }
                        }
                        _ => {
                            panic!("unsupported view provider request: {:?}", request)
                        }
                    }
                    // We only support a single view, so we'll stop handling
                    // CreateView requests after we create the first view.
                    while let Some(request) = stream.try_next().await.unwrap() {
                        panic!("unsupported view provider request: {:?}", request)
                    }
                    break;
                }
                task_queue.post(|_client| {
                    // Returning an error causes the client connection to be
                    // closed (and that typically closes the application).
                    Err(format_err!("View provider channel closed "))
                });
                Ok(())
            }
            .unwrap_or_else(|e: Error| println!("{:?}", e)),
        )
        .detach();
        Ok(control_handle)
    }

    fn spawn_keyboard_listener(
        _this: ObjectRef<Self>,
        surface_ref: ObjectRef<Surface>,
        mut view_ref: ViewRef,
        task_queue: TaskQueue,
    ) -> Result<(), Error> {
        let keyboard = connect_to_protocol::<fidl_fuchsia_ui_input3::KeyboardMarker>()?;
        let (listener_client_end, mut listener_stream) =
            create_request_stream::<fidl_fuchsia_ui_input3::KeyboardListenerMarker>()?;

        fasync::Task::local(async move {
            keyboard.add_listener(&mut view_ref, listener_client_end).await.unwrap();

            while let Some(event) = listener_stream.try_next().await.unwrap() {
                match event {
                    fidl_fuchsia_ui_input3::KeyboardListenerRequest::OnKeyEvent {
                        event,
                        responder,
                        ..
                    } => {
                        responder
                            .send(fidl_fuchsia_ui_input3::KeyEventStatus::Handled)
                            .expect("send");
                        task_queue.post(move |client| {
                            client.input_dispatcher.handle_key_event(surface_ref, &event)?;
                            Ok(())
                        });
                    }
                }
            }
        })
        .detach();
        Ok(())
    }

    pub fn shutdown(&self, client: &Client) {
        self.view_provider_controller.as_ref().map(|h| h.shutdown());
        self.session_listener_controller.as_ref().map(|h| h.shutdown());
        self.view.as_ref().map(|v| v.lock().shutdown());
        client.display().delete_view_provider(self.view_id);
    }
}

impl RequestReceiver<ZxdgToplevelV6> for XdgToplevel {
    fn receive(
        this: ObjectRef<Self>,
        request: ZxdgToplevelV6Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZxdgToplevelV6Request::Destroy => {
                client.xdg_toplevels.remove(this);
                let surface_ref = {
                    let this = this.get(client)?;
                    this.shutdown(client);
                    this.surface_ref
                };
                // We need to present here to commit the removal of our
                // toplevel. This will inform our parent that our view has
                // been destroyed.
                Surface::present(surface_ref, client, vec![])?;

                #[cfg(feature = "flatland")]
                surface_ref.get_mut(client)?.clear_flatland();
                #[cfg(not(feature = "flatland"))]
                surface_ref.get_mut(client)?.clear_session();
                client.delete_id(this.id())?;
            }
            ZxdgToplevelV6Request::SetParent { .. } => {}
            ZxdgToplevelV6Request::SetTitle { .. } => {}
            ZxdgToplevelV6Request::SetAppId { .. } => {}
            ZxdgToplevelV6Request::ShowWindowMenu { .. } => {}
            ZxdgToplevelV6Request::Move { .. } => {}
            ZxdgToplevelV6Request::Resize { .. } => {}
            ZxdgToplevelV6Request::SetMaxSize { .. } => {}
            ZxdgToplevelV6Request::SetMinSize { .. } => {}
            ZxdgToplevelV6Request::SetMaximized => {}
            ZxdgToplevelV6Request::UnsetMaximized => {}
            ZxdgToplevelV6Request::SetFullscreen { .. } => {}
            ZxdgToplevelV6Request::UnsetFullscreen => {}
            ZxdgToplevelV6Request::SetMinimized => {}
        }
        Ok(())
    }
}

/// A scenic view implementation to back an |XdgToplevel| resource.
///
/// An `XdgToplevelView` will be created by the `ViewProvider` for an
/// `XdgToplevel`.
struct XdgToplevelView {
    #[cfg(feature = "flatland")]
    flatland: Flatland,
    #[cfg(not(feature = "flatland"))]
    view: Option<View>,
    #[cfg(not(feature = "flatland"))]
    session: ScenicSession,
    #[cfg(not(feature = "flatland"))]
    background_node: ShapeNode,
    #[cfg(not(feature = "flatland"))]
    container_node: EntityNode,
    logical_size: SizeF,
    task_queue: TaskQueue,
    toplevel: ObjectRef<XdgToplevel>,
    surface: ObjectRef<Surface>,
}

type XdgToplevelViewPtr = Arc<Mutex<XdgToplevelView>>;

impl XdgToplevelView {
    pub fn physical_size(&self, client: &Client) -> Size {
        let pixel_scale = self.surface.get(client).unwrap().pixel_scale();
        Size {
            width: (self.logical_size.width * pixel_scale.0).round() as i32,
            height: (self.logical_size.height * pixel_scale.1).round() as i32,
        }
    }

    fn finish_setup_scene(view_controller: &XdgToplevelViewPtr) {
        ftrace::duration!("wayland", "XdgToplevelView::finish_setup_scene");
        let mut vc = view_controller.lock();
        vc.setup_scene();
        vc.present_internal();
    }

    fn present_internal(&mut self) {
        let surface_ref = self.surface;
        self.task_queue.post(move |client| Surface::present(surface_ref, client, vec![]));
    }

    fn reconfigure(&self) {
        // If we have both a size and a pixel scale, we're ready to send the
        // configure event to the client. We need both because we send expose
        // physical pixels to the client.
        if self.logical_size.width != 0.0 && self.logical_size.width != 0.0 {
            // Post the xdg_toplevel::configure event to inform the client about
            // the change.
            let toplevel = self.toplevel;
            self.task_queue.post(move |client| XdgToplevel::configure(toplevel, client))
        }
    }
}

#[cfg(feature = "flatland")]
impl XdgToplevelView {
    pub fn new(
        flatland: Flatland,
        task_queue: TaskQueue,
        toplevel: ObjectRef<XdgToplevel>,
        surface: ObjectRef<Surface>,
    ) -> Result<XdgToplevelViewPtr, Error> {
        let view_controller = XdgToplevelView {
            flatland: flatland.clone(),
            logical_size: SizeF { width: 0.0, height: 0.0 },
            task_queue,
            toplevel,
            surface,
        };
        let view_controller = Arc::new(Mutex::new(view_controller));
        Self::finish_setup_scene(&view_controller);
        Ok(view_controller)
    }

    pub fn shutdown(&mut self) {}

    /// Attach the wl_surface to this view by setting the surface's transform
    /// as the root transform.
    pub fn attach(&self, surface: ObjectRef<Surface>, client: &Client) {
        ftrace::duration!("wayland", "XdgToplevelView::attach");
        if let Ok(surface) = surface.get(client) {
            if let Some(transform) = surface.transform() {
                self.flatland
                    .proxy()
                    .set_root_transform(&mut transform.clone())
                    .expect("fidl error");
            }
        }
    }

    fn setup_scene(&self) {
        ftrace::duration!("wayland", "XdgToplevelView::setup_scene");
    }

    fn update(&mut self) {
        ftrace::duration!("wayland", "XdgToplevelView::update");
        self.present_internal();
    }

    pub fn handle_layout_changed(&mut self, logical_size: &SizeF) {
        ftrace::duration!("wayland", "XdgToplevelView::handle_layout_changed");
        self.logical_size = *logical_size;
        self.update();
        self.reconfigure();
    }
}

#[cfg(not(feature = "flatland"))]
impl XdgToplevelView {
    pub fn new(
        view: View,
        session: ScenicSession,
        task_queue: TaskQueue,
        toplevel: ObjectRef<XdgToplevel>,
        surface: ObjectRef<Surface>,
    ) -> Result<XdgToplevelViewPtr, Error> {
        let view_controller = XdgToplevelView {
            view: Some(view),
            session: session.clone(),
            background_node: ShapeNode::new(session.as_inner().clone()),
            container_node: EntityNode::new(session.as_inner().clone()),
            logical_size: SizeF { width: 0.0, height: 0.0 },
            task_queue,
            toplevel,
            surface,
        };
        let view_controller = Arc::new(Mutex::new(view_controller));
        Self::finish_setup_scene(&view_controller);
        Ok(view_controller)
    }

    pub fn shutdown(&mut self) {
        self.view = None;
    }

    /// Attach the wl_surface to this view by inserting the surfaces node into
    /// the view.
    pub fn attach(&self, surface: ObjectRef<Surface>, client: &Client) {
        ftrace::duration!("wayland", "XdgToplevelView::attach");
        if let Ok(surface) = surface.get(client) {
            if let Some(node) = surface.node() {
                self.container_node.add_child(node);
            }
        }
    }

    fn setup_scene(&self) {
        ftrace::duration!("wayland", "XdgToplevelView::setup_scene");
        self.view.as_ref().map(|v| {
            v.add_child(&self.background_node);
            v.add_child(&self.container_node);
        });

        self.container_node.resource().set_event_mask(gfx::METRICS_EVENT_MASK);

        let material = Material::new(self.session.as_inner().clone());
        material.set_color(ColorRgba { red: 0x40, green: 0x40, blue: 0x40, alpha: 0x80 });
        self.background_node.set_material(&material);
    }

    fn update(&mut self) {
        ftrace::duration!("wayland", "XdgToplevelView::update");
        let center_x = self.logical_size.width * 0.5;
        let center_y = self.logical_size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            self.session.as_inner().clone(),
            self.logical_size.width,
            self.logical_size.height,
        ));
        // Place the container node above the background.
        self.background_node.set_translation(center_x, center_y, 0.0);
        self.container_node.set_translation(0.0, 0.0, -1.0);
        self.present_internal();
    }

    pub fn set_pixel_scale(&mut self, scale_x: f32, scale_y: f32) {
        let surface_ref = self.surface;
        self.task_queue.post(move |client| {
            surface_ref.get_mut(client)?.set_pixel_scale(scale_x, scale_y);
            Ok(())
        });
        self.update();
        self.reconfigure();
    }

    pub fn handle_properies_changed(&mut self, properties: &fidl_fuchsia_ui_gfx::ViewProperties) {
        ftrace::duration!("wayland", "XdgToplevelView::handle_properies_changed");
        let width = properties.bounding_box.max.x - properties.bounding_box.min.x;
        let height = properties.bounding_box.max.y - properties.bounding_box.min.y;
        self.logical_size = SizeF { width, height };
        self.update();
        self.reconfigure();
    }
}
