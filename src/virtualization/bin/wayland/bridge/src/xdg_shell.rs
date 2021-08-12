// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::{
    atomic::{AtomicUsize, Ordering},
    Arc,
};

use anyhow::{format_err, Error};
use fidl::endpoints::{create_endpoints, create_request_stream, RequestStream, ServerEnd};
use fidl_fuchsia_math::{Rect, Size, SizeF};
use fidl_fuchsia_ui_app::{ViewProviderControlHandle, ViewProviderMarker, ViewProviderRequest};
use fidl_fuchsia_ui_gfx::{self as gfx, ColorRgba};
use fidl_fuchsia_ui_scenic::{
    SessionListenerControlHandle, SessionListenerMarker, SessionListenerRequest,
};
use fidl_fuchsia_ui_views::{ViewRef, ViewToken};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_scenic::{EntityNode, Material, Rectangle, ShapeNode, View};
use fuchsia_trace as ftrace;
use fuchsia_wayland_core as wl;
use futures::prelude::*;
use parking_lot::Mutex;
use zxdg_shell_v6::{
    zxdg_toplevel_v6, ZxdgPopupV6, ZxdgPopupV6Event, ZxdgPopupV6Request, ZxdgPositionerV6,
    ZxdgPositionerV6Request, ZxdgShellV6, ZxdgShellV6Request, ZxdgSurfaceV6, ZxdgSurfaceV6Event,
    ZxdgSurfaceV6Request, ZxdgToplevelV6, ZxdgToplevelV6Event, ZxdgToplevelV6Request,
};

use crate::client::{Client, TaskQueue};
use crate::compositor::{Surface, SurfaceCommand, SurfaceRole};
use crate::object::{NewObjectExt, ObjectRef, RequestReceiver};
use crate::scenic::ScenicSession;

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
    pub fn finalize_commit(&self) -> bool {
        ftrace::duration!("wayland", "XdgSurface::finalize_commit");
        match self.xdg_role {
            Some(XdgSurfaceRole::Toplevel(_)) => true,
            _ => false,
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
    session_listener_controller: Option<SessionListenerControlHandle>,
    /// Identifier for the view.
    view_id: u32,
}

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
        })
    }

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
                .unwrap_or(Size { width: 0, height: 0 });
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
            fidl_fuchsia_ui_gfx::Event::ViewDetachedFromScene(
                fidl_fuchsia_ui_gfx::ViewDetachedFromSceneEvent { view_id: _ },
            ) => task_queue.post(move |_client| {
                // Returning an error causes the client connection to be closed.
                Err(format_err!("View detached"))
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

    #[allow(unreachable_patterns)]
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
                    let token_and_view_ref = match request {
                        ViewProviderRequest::CreateView { token, .. } => {
                            Some((token, fuchsia_scenic::ViewRefPair::new()?))
                        }
                        ViewProviderRequest::CreateViewWithViewRef {
                            token,
                            view_ref_control,
                            view_ref,
                            ..
                        } => Some((
                            token,
                            fuchsia_scenic::ViewRefPair { control_ref: view_ref_control, view_ref },
                        )),
                        _ => None,
                    };
                    let (token, view_ref_pair) = token_and_view_ref.unwrap();
                    let view_token = ViewToken { value: token };
                    let view_ref = fuchsia_scenic::duplicate_view_ref(&view_ref_pair.view_ref)?;
                    let view = View::new3(
                        session.as_inner().clone(),
                        view_token,
                        view_ref_pair.control_ref,
                        view_ref_pair.view_ref,
                        Some(String::from("Wayland View")),
                    );
                    XdgToplevel::spawn_keyboard_listener(
                        this,
                        surface_ref,
                        view_ref,
                        task_queue.clone(),
                    )?;
                    let view_ptr =
                        XdgToplevelView::new(view, session, task_queue.clone(), this, surface_ref)?;
                    {
                        let view_ptr = view_ptr.clone();
                        task_queue.post(move |client| {
                            let surface_ref = this.get(client)?.surface_ref;
                            view_ptr.lock().attach(surface_ref, client);
                            this.get_mut(client)?.set_view(view_ptr.clone());
                            Ok(())
                        });
                    }
                    // We only support a single view, so we'll stop handling
                    // CreateView requests after we create the first view.
                    break;
                }
                Ok(())
            }
            .unwrap_or_else(|e: Error| println!("{:?}", e)),
        )
        .detach();
        Ok(control_handle)
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
    view: Option<View>,
    session: ScenicSession,
    background_node: ShapeNode,
    container_node: EntityNode,
    logical_size: SizeF,
    task_queue: TaskQueue,
    toplevel: ObjectRef<XdgToplevel>,
    surface: ObjectRef<Surface>,
}

type XdgToplevelViewPtr = Arc<Mutex<XdgToplevelView>>;

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

    pub fn physical_size(&self, client: &Client) -> Size {
        let pixel_scale = self.surface.get(client).unwrap().pixel_scale();
        Size {
            width: (self.logical_size.width * pixel_scale.0).round() as i32,
            height: (self.logical_size.height * pixel_scale.1).round() as i32,
        }
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

    fn finish_setup_scene(view_controller: &XdgToplevelViewPtr) {
        ftrace::duration!("wayland", "XdgToplevelView::finish_setup_scene");
        let mut vc = view_controller.lock();
        vc.setup_scene();
        vc.present_internal();
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

    fn present_internal(&mut self) {
        let surface_ref = self.surface;
        self.task_queue.post(move |client| Surface::present(surface_ref, client, vec![]));
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
