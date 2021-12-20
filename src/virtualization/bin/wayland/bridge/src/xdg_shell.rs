// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::{Client, TaskQueue},
    crate::compositor::{Surface, SurfaceCommand, SurfaceRole},
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    anyhow::{format_err, Error},
    fidl::endpoints::{create_endpoints, create_proxy, create_request_stream, ServerEnd},
    fidl::prelude::*,
    fidl_fuchsia_element::{
        Annotation, AnnotationKey, AnnotationValue, ViewControllerMarker, ViewControllerProxy,
        ViewSpec,
    },
    fidl_fuchsia_math::{Rect, Size, SizeF},
    fidl_fuchsia_ui_app::{ViewProviderControlHandle, ViewProviderMarker, ViewProviderRequest},
    fidl_fuchsia_ui_views::ViewRef,
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_scenic::ViewRefPair,
    fuchsia_trace as ftrace, fuchsia_wayland_core as wl,
    fuchsia_wayland_core::Enum,
    futures::prelude::*,
    parking_lot::Mutex,
    std::collections::VecDeque,
    std::sync::{
        atomic::{AtomicUsize, Ordering},
        Arc,
    },
    zxdg_shell_v6::{
        zxdg_positioner_v6::{Anchor, Gravity},
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
    fidl_fuchsia_math::{SizeU, Vec_},
    fidl_fuchsia_ui_composition::{
        ChildViewWatcherMarker, ChildViewWatcherProxy, ContentId, FlatlandEvent,
        FlatlandEventStream, FlatlandMarker, ParentViewportWatcherMarker,
        ParentViewportWatcherProxy, TransformId, ViewportProperties,
    },
    fidl_fuchsia_ui_views::{
        ViewIdentityOnCreation, ViewRefFocusedMarker, ViewRefFocusedProxy, ViewportCreationToken,
    },
    fuchsia_scenic::flatland::{LinkTokenPair, ViewBoundProtocols},
    std::collections::BTreeSet,
};

#[cfg(not(feature = "flatland"))]
use {
    crate::scenic::ScenicSession,
    crate::seat::InputDispatcher,
    fidl_fuchsia_ui_gfx::{self as gfx, ColorRgba},
    fidl_fuchsia_ui_scenic::{
        SessionListenerControlHandle, SessionListenerMarker, SessionListenerRequest,
    },
    fidl_fuchsia_ui_views::{ViewHolderToken, ViewToken},
    fuchsia_scenic::{EntityNode, Material, Rectangle, ShapeNode, View, ViewHolder, ViewTokenPair},
    std::collections::BTreeMap,
};

// This must start at 1 to satisfy the ViewProducer protocol.
static NEXT_VIEW_ID: AtomicUsize = AtomicUsize::new(1);

// Annotations namespace for title.
static TITLE_ANNOTATION_NS: &'static str = "ermine";

// Title annotation value.
static TITLE_ANNOTATION_VALUE: &'static str = "name";

///
/// Popup, dialog, and multiple toplevel window status:
///
/// The view system on Fuchsia is lacking support for these
/// type of window management features today so we emulate them
/// using child views.
///
/// Here are the child surface features currently implemented:
///
/// - XDG shell popups with static placement. Configured to only
///   occupy the the desired area.
/// - XDG shell toplevels without a parent. Configured to occupy
///   the fullscreen area.
/// - XDG shell toplevels with parent and dynamic offset set
///   using the aura shell interface. Configured to only occupy
///   the the desired area. This is used to implement X11
///   override-redirect windows for tooltips and menus.
/// - XDG shell toplevels without a parent but created after
///   a view provider has already been created. These toplevels
///   are created as child views of the root XDG surface.
/// - Window controls are missing but XDG surfaces can be closed
///   by pressing Escape key three times quickly.
///

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
                id.implement(client, XdgPositioner::new())?;
            }
            ZxdgShellV6Request::Pong { .. } => {}
        }
        Ok(())
    }
}

pub struct XdgPositioner {
    size: Size,
    anchor_rect: Rect,
    anchor: Enum<Anchor>,
    gravity: Enum<Gravity>,
    offset: (i32, i32),
}

impl XdgPositioner {
    pub fn new() -> Self {
        Self {
            size: Size { width: 0, height: 0 },
            anchor_rect: Rect { x: 0, y: 0, width: 0, height: 0 },
            anchor: Enum::Recognized(Anchor::None),
            gravity: Enum::Recognized(Gravity::None),
            offset: (0, 0),
        }
    }

    pub fn get_geometry(&self) -> Result<Rect, Error> {
        let mut geometry = Rect {
            x: self.offset.0,
            y: self.offset.1,
            width: self.size.width,
            height: self.size.height,
        };

        let anchor = self.anchor.as_enum()?.bits();
        geometry.x += if (anchor & Anchor::Left.bits()) != 0 {
            self.anchor_rect.x
        } else if (anchor & Anchor::Right.bits()) != 0 {
            self.anchor_rect.x + self.anchor_rect.width
        } else {
            self.anchor_rect.x + self.anchor_rect.width / 2
        };

        geometry.y += if (anchor & Anchor::Top.bits()) != 0 {
            self.anchor_rect.y
        } else if (anchor & Anchor::Bottom.bits()) != 0 {
            self.anchor_rect.y + self.anchor_rect.height
        } else {
            self.anchor_rect.y + self.anchor_rect.height / 2
        };

        let gravity = self.gravity.as_enum()?.bits();
        geometry.x -= if (gravity & Gravity::Left.bits()) != 0 {
            geometry.width
        } else if (gravity & Gravity::Right.bits()) != 0 {
            0
        } else {
            geometry.width / 2
        };

        geometry.y -= if (gravity & Gravity::Top.bits()) != 0 {
            geometry.height
        } else if (gravity & Gravity::Bottom.bits()) != 0 {
            0
        } else {
            geometry.height / 2
        };

        Ok(geometry)
    }

    fn set_size(&mut self, width: i32, height: i32) {
        self.size = Size { width, height };
    }

    fn set_anchor_rect(&mut self, x: i32, y: i32, width: i32, height: i32) {
        self.anchor_rect = Rect { x, y, width, height };
    }

    fn set_anchor(&mut self, anchor: Enum<Anchor>) {
        self.anchor = anchor;
    }

    fn set_gravity(&mut self, gravity: Enum<Gravity>) {
        self.gravity = gravity;
    }

    fn set_offset(&mut self, x: i32, y: i32) {
        self.offset = (x, y);
    }
}

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
            ZxdgPositionerV6Request::SetSize { width, height } => {
                if width <= 0 || height <= 0 {
                    return Err(format_err!(
                        "invalid_input error width={:?} height={:?}",
                        width,
                        height
                    ));
                }
                this.get_mut(client)?.set_size(width, height);
            }
            ZxdgPositionerV6Request::SetAnchorRect { x, y, width, height } => {
                if width <= 0 || height <= 0 {
                    return Err(format_err!(
                        "invalid_input error width={:?} height={:?}",
                        width,
                        height
                    ));
                }
                this.get_mut(client)?.set_anchor_rect(x, y, width, height);
            }
            ZxdgPositionerV6Request::SetAnchor { anchor } => {
                this.get_mut(client)?.set_anchor(anchor);
            }
            ZxdgPositionerV6Request::SetGravity { gravity } => {
                this.get_mut(client)?.set_gravity(gravity);
            }
            ZxdgPositionerV6Request::SetConstraintAdjustment { .. } => {}
            ZxdgPositionerV6Request::SetOffset { x, y } => {
                this.get_mut(client)?.set_offset(x, y);
            }
        }
        Ok(())
    }
}

const ESCAPE_DELAY_NS: i64 = 1_000_000_000;

/// An `XdgSurface` is the common base to the different surfaces in the
/// `XdgShell` (ex: `XdgToplevel`, `XdgPopup`).
pub struct XdgSurface {
    /// A reference to the underlying `Surface` for this `XdgSurface`.
    surface_ref: ObjectRef<Surface>,
    /// A reference to the root `Surface` for this `XdgSurface`. The root surface
    /// is the surface that can receive keyboard focus.
    root_surface_ref: ObjectRef<Surface>,
    /// The sub-role assigned to this `XdgSurface`. This is needed because the
    /// `XdgSurface` is not a role itself, but a base for the concrete XDG
    /// surface roles.
    xdg_role: Option<XdgSurfaceRole>,
    /// The associated scenic view for this `XdgSurface`. This will be
    /// populated in response to requests to the public `ViewProvider` service,
    /// or by creating an internal child view.
    view: Option<XdgSurfaceViewPtr>,
}

impl XdgSurface {
    /// Creates a new `XdgSurface`.
    pub fn new(id: wl::ObjectId) -> Self {
        XdgSurface {
            surface_ref: id.into(),
            root_surface_ref: id.into(),
            xdg_role: None,
            view: None,
        }
    }

    /// Returns a reference to the underlying `Surface` for this `XdgSurface`.
    pub fn surface_ref(&self) -> ObjectRef<Surface> {
        self.surface_ref
    }

    /// Returns a reference to the root `Surface` for this `XdgSurface`.
    pub fn root_surface_ref(&self) -> ObjectRef<Surface> {
        self.root_surface_ref
    }

    /// Sets the concrete role for this `XdgSurface`.
    ///
    /// Similar to `Surface`, an `XdgSurface` isn't of much use until a role
    /// has been assigned.
    pub fn set_xdg_role(&mut self, xdg_role: XdgSurfaceRole) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurface::set_xdg_role");
        // The role is valid unless a different role has been assigned before.
        let valid_role = match &self.xdg_role {
            Some(XdgSurfaceRole::Popup(_)) => match xdg_role {
                XdgSurfaceRole::Popup(_) => true,
                _ => false,
            },
            Some(XdgSurfaceRole::Toplevel(_)) => match xdg_role {
                XdgSurfaceRole::Toplevel(_) => true,
                _ => false,
            },
            _ => true,
        };
        if valid_role {
            self.xdg_role = Some(xdg_role);
            Ok(())
        } else {
            Err(format_err!(
                "Attemping to re-assign xdg_surface role from {:?} to {:?}",
                self.xdg_role,
                xdg_role
            ))
        }
    }

    /// Sets the backing view for this `XdgSurface`.
    fn set_view(&mut self, view: XdgSurfaceViewPtr) {
        // We shut down the ViewProvider after creating the first view, so this
        // should never happen.
        assert!(self.view.is_none());
        self.view = Some(view);
    }

    /// Sets the root surface for this `XdgSurface`.
    fn set_root_surface(&mut self, root_surface_ref: ObjectRef<Surface>) {
        self.root_surface_ref = root_surface_ref;
    }

    /// Performs a surface configuration sequence.
    ///
    /// Each concrete `XdgSurface` role configuration sequence is concluded and
    /// committed by a xdg_surface::configure event.
    pub fn configure(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurface::configure");
        let xdg_surface = this.get(client)?;
        match xdg_surface.xdg_role {
            Some(XdgSurfaceRole::Popup(popup)) => {
                XdgPopup::configure(popup, client)?;
            }
            Some(XdgSurfaceRole::Toplevel(toplevel)) => {
                XdgToplevel::configure(toplevel, client)?;
            }
            _ => {}
        }
        let serial = client.event_queue().next_serial();
        client.event_queue().post(this.id(), ZxdgSurfaceV6Event::Configure { serial })?;
        Ok(())
    }

    /// Handle a commit request to this `XdgSurface`.
    ///
    /// This will be triggered by a wl_surface::commit request to the backing
    /// wl_surface object for this xdg_surface, and simply delegates the request
    /// to the concrete surface.
    pub fn finalize_commit(this: ObjectRef<Self>, client: &mut Client) -> Result<bool, Error> {
        ftrace::duration!("wayland", "XdgSurface::finalize_commit");
        if let Ok(xdg_surface) = this.get(client) {
            match xdg_surface.xdg_role {
                Some(XdgSurfaceRole::Popup(_)) => Ok(true),
                Some(XdgSurfaceRole::Toplevel(toplevel)) => {
                    XdgToplevel::finalize_commit(toplevel, client)
                }
                _ => Ok(false),
            }
        } else {
            Ok(false)
        }
    }

    pub fn shutdown(&self, client: &Client) {
        ftrace::duration!("wayland", "XdgSurface::shutdown");
        self.view.as_ref().map(|v| v.lock().shutdown());
        match self.xdg_role {
            Some(XdgSurfaceRole::Popup(popup)) => {
                if let Ok(popup) = popup.get(client) {
                    popup.shutdown();
                }
            }
            Some(XdgSurfaceRole::Toplevel(toplevel)) => {
                if let Ok(toplevel) = toplevel.get(client) {
                    toplevel.shutdown(client);
                }
            }
            _ => {}
        }
    }

    fn spawn_keyboard_listener(
        surface_ref: ObjectRef<Surface>,
        mut view_ref: ViewRef,
        task_queue: TaskQueue,
    ) -> Result<(), Error> {
        let keyboard = connect_to_protocol::<fidl_fuchsia_ui_input3::KeyboardMarker>()?;
        let (listener_client_end, mut listener_stream) =
            create_request_stream::<fidl_fuchsia_ui_input3::KeyboardListenerMarker>()?;

        fasync::Task::local(async move {
            keyboard.add_listener(&mut view_ref, listener_client_end).await.unwrap();

            // Track the event time of the last three Escape key presses.
            let mut escapes: VecDeque<_> = vec![0; 3].into_iter().collect();

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
                        // Store the event time of the last three Escape key presses
                        // and attempt to close `XdgSurface` if the elapsed time
                        // between the first and the last press is less than
                        // ESCAPE_DELAY_NS.
                        let close = if event.type_
                            == Some(fidl_fuchsia_ui_input3::KeyEventType::Pressed)
                            && event.key == Some(fidl_fuchsia_input::Key::Escape)
                        {
                            let timestamp = event.timestamp.expect("missing timestamp");
                            escapes.pop_front();
                            escapes.push_back(timestamp);
                            // Same to unwrap as there is always three elements.
                            let elapsed = escapes.back().unwrap() - escapes.front().unwrap();
                            if elapsed < ESCAPE_DELAY_NS {
                                escapes = vec![0; 3].into_iter().collect();
                                true
                            } else {
                                false
                            }
                        } else {
                            false
                        };

                        task_queue.post(move |client| {
                            if close {
                                // Close focused XDG surface.
                                for xdg_surface_ref in &client.xdg_surfaces {
                                    let xdg_surface = xdg_surface_ref.get(client)?;
                                    let surface_ref = xdg_surface.surface_ref;
                                    if client.input_dispatcher.has_focus(surface_ref) {
                                        XdgSurface::close(*xdg_surface_ref, client)?;
                                        break;
                                    }
                                }
                            } else {
                                client.input_dispatcher.handle_key_event(surface_ref, &event)?;
                            }
                            Ok(())
                        });
                    }
                }
            }
        })
        .detach();
        Ok(())
    }

    fn get_event_target(
        root_surface_ref: ObjectRef<Surface>,
        client: &Client,
    ) -> Option<ObjectRef<Self>> {
        for xdg_surface_ref in client.xdg_surfaces.iter().rev() {
            if let Ok(xdg_surface) = xdg_surface_ref.get(client) {
                if xdg_surface.root_surface_ref == root_surface_ref {
                    return Some(*xdg_surface_ref);
                }
            }
        }
        None
    }

    fn close(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        match this.get(client)?.xdg_role {
            Some(XdgSurfaceRole::Popup(popup)) => XdgPopup::close(popup, client),
            Some(XdgSurfaceRole::Toplevel(toplevel)) => XdgToplevel::close(toplevel, client),
            _ => Ok(()),
        }
    }
}

#[cfg(feature = "flatland")]
impl XdgSurface {
    /// Adds a child view to this `XdgSurface`.
    fn add_child_view(
        this: ObjectRef<Self>,
        client: &mut Client,
        viewport_creation_token: ViewportCreationToken,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurface::add_child_view");
        let xdg_surface = this.get(client)?;
        let surface = xdg_surface.surface_ref().get(client)?;
        let flatland = surface
            .flatland()
            .ok_or(format_err!("Unable to create a child view without a flatland instance."))?;
        let transform = flatland.alloc_transform_id();
        let task_queue = client.task_queue();
        let (child_view_watcher, server_end) = create_proxy::<ChildViewWatcherMarker>()
            .expect("failed to create ChildViewWatcher endpoints");
        XdgSurface::spawn_child_view_listener(
            this,
            child_view_watcher,
            task_queue.clone(),
            transform.value,
        );
        if let Some(view) = this.get_mut(client)?.view.clone() {
            view.lock().add_child_view(transform.value, viewport_creation_token, server_end);
        }
        Ok(())
    }

    fn spawn_child_view(
        this: ObjectRef<Self>,
        client: &mut Client,
        flatland: Flatland,
        parent_ref: ObjectRef<Self>,
        local_offset: Option<(i32, i32)>,
        geometry: Rect,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurface::spawn_child_view");
        let mut link_tokens = LinkTokenPair::new().expect("failed to create LinkTokenPair");
        let parent = parent_ref.get(client)?;
        let parent_view = parent.view.clone();
        let root_surface_ref = parent.root_surface_ref();
        Self::add_child_view(parent_ref, client, link_tokens.viewport_creation_token)?;
        let xdg_surface = this.get(client)?;
        let surface_ref = xdg_surface.surface_ref();
        let task_queue = client.task_queue();
        let (parent_viewport_watcher, server_end) = create_proxy::<ParentViewportWatcherMarker>()
            .expect("failed to create ParentViewportWatcherProxy");
        flatland
            .proxy()
            .create_view(&mut link_tokens.view_creation_token, server_end)
            .expect("fidl error");
        XdgSurface::spawn_parent_viewport_listener(
            this,
            parent_viewport_watcher,
            task_queue.clone(),
        );
        let view_ptr = XdgSurfaceView::new(
            flatland,
            task_queue.clone(),
            this,
            surface_ref,
            parent_view,
            local_offset,
            geometry,
        )?;
        XdgSurfaceView::finish_setup_scene(&view_ptr, client)?;
        let xdg_surface = this.get_mut(client)?;
        xdg_surface.set_view(view_ptr.clone());
        xdg_surface.set_root_surface(root_surface_ref);
        Ok(())
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
                                let time_in_ms =
                                    (info.presentation_time.expect("no presentation time")
                                        / 1_000_000) as u32;
                                // TODO: Remove this check when OnNextFrameBegin is only sent as a
                                // result of Present.
                                if let Some(callbacks) =
                                    surface_ref.get_mut(client)?.next_callbacks()
                                {
                                    callbacks.iter().try_for_each(|callback| {
                                        Callback::done(*callback, client, time_in_ms)?;
                                        client.delete_id(callback.id())
                                    })?;
                                }
                                Surface::add_present_credits(
                                    surface_ref,
                                    client,
                                    values.additional_present_credits.unwrap_or(0),
                                )
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
            HangingGetStream::new(parent_viewport_watcher, ParentViewportWatcherProxy::get_layout);

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
                            if let Some(view) = this.get(client)?.view.clone() {
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
                        println!("parent viewport GetLayout() error: {:?}", fidl_error);
                        return;
                    }
                }
            }
        })
        .detach();
    }

    fn spawn_child_view_listener(
        this: ObjectRef<Self>,
        child_view_watcher: ChildViewWatcherProxy,
        task_queue: TaskQueue,
        id: u64,
    ) {
        let mut status_stream =
            HangingGetStream::new(child_view_watcher, ChildViewWatcherProxy::get_status);

        fasync::Task::local(async move {
            while let Some(result) = status_stream.next().await {
                match result {
                    Ok(_status) => {}
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
                        let xdg_surface_ref = this;
                        task_queue.post(move |client| {
                            if let Some(view) = xdg_surface_ref.get(client)?.view.clone() {
                                view.lock().handle_view_disconnected(id);
                            }
                            Ok(())
                        });
                        return;
                    }
                    Err(fidl_error) => {
                        println!("child view GetStatus() error: {:?}", fidl_error);
                        return;
                    }
                }
            }
        })
        .detach();
    }

    fn spawn_view_ref_focused_listener(
        this: ObjectRef<Self>,
        source_surface_ref: ObjectRef<Surface>,
        view_ref_focused: ViewRefFocusedProxy,
        task_queue: TaskQueue,
    ) {
        let mut focus_state_stream =
            HangingGetStream::new(view_ref_focused, ViewRefFocusedProxy::watch);

        fasync::Task::local(async move {
            while let Some(result) = focus_state_stream.next().await {
                match result {
                    Ok(focus_state) => {
                        task_queue.post(move |client| {
                            let root_surface_ref = this.get(client)?.root_surface_ref;
                            let xdg_surface_ref =
                                XdgSurface::get_event_target(root_surface_ref, client)
                                    .unwrap_or(this);
                            let surface_ref = xdg_surface_ref.get(client)?.surface_ref;
                            if surface_ref.get(client).is_ok() {
                                let had_focus = client.input_dispatcher.has_focus(surface_ref);
                                client.input_dispatcher.handle_keyboard_focus(
                                    source_surface_ref,
                                    surface_ref,
                                    focus_state.focused.unwrap(),
                                )?;
                                let has_focus = client.input_dispatcher.has_focus(surface_ref);
                                if had_focus != has_focus {
                                    // If our focus has changed we need to reconfigure so that the
                                    // Activated flag can be set or cleared.
                                    Self::configure(xdg_surface_ref, client)?;
                                }
                            }
                            Ok(())
                        });
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
                        task_queue
                            .post(|_client| Err(format_err!("ViewRefFocused channel closed")));
                        return;
                    }
                    Err(fidl_error) => {
                        println!("ViewRefFocused Watch() error: {:?}", fidl_error);
                        return;
                    }
                }
            }
        })
        .detach();
    }
}

#[cfg(feature = "flatland")]
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
                let proxy =
                    connect_to_protocol::<FlatlandMarker>().expect("error connecting to Flatland");
                let flatland = Flatland::new(proxy);
                let toplevel = XdgToplevel::new(this, client, flatland.clone())?;
                let toplevel_ref = id.implement(client, toplevel)?;
                this.get_mut(client)?.set_xdg_role(XdgSurfaceRole::Toplevel(toplevel_ref))?;
                XdgSurface::spawn_flatland_listener(
                    this,
                    client,
                    flatland.proxy().take_event_stream(),
                )?;
            }
            ZxdgSurfaceV6Request::GetPopup { id, parent, positioner } => {
                let proxy =
                    connect_to_protocol::<FlatlandMarker>().expect("error connecting to Flatland");
                let flatland = Flatland::new(proxy);
                let popup = XdgPopup::new(this, client, flatland.clone(), positioner.into())?;
                let geometry = popup.geometry();
                let popup_ref = id.implement(client, popup)?;
                let xdg_surface = this.get_mut(client)?;
                xdg_surface.set_xdg_role(XdgSurfaceRole::Popup(popup_ref))?;
                XdgSurface::spawn_child_view(
                    this,
                    client,
                    flatland.clone(),
                    parent.into(),
                    Some((geometry.x, geometry.y)),
                    geometry,
                )?;
                XdgSurface::spawn_flatland_listener(
                    this,
                    client,
                    flatland.proxy().take_event_stream(),
                )?;
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

#[cfg(not(feature = "flatland"))]
impl XdgSurface {
    /// Adds a child view to this `XdgSurface`.
    fn add_child_view(
        this: ObjectRef<Self>,
        client: &mut Client,
        view_holder_token: ViewHolderToken,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurface::add_child_view");
        let xdg_surface = this.get(client)?;
        let surface = xdg_surface.surface_ref().get(client)?;
        let session =
            surface.session().ok_or(format_err!("Unable to present surface without a session."))?;
        let view_holder = ViewHolder::new(
            session.as_inner().clone(),
            view_holder_token,
            Some(String::from("Wayland Popup View Holder")),
        );
        if let Some(view) = this.get_mut(client)?.view.clone() {
            view.lock().add_child_view(view_holder);
        }
        Ok(())
    }

    fn spawn_child_view(
        this: ObjectRef<Self>,
        client: &mut Client,
        session: ScenicSession,
        parent_ref: ObjectRef<Self>,
        local_offset: Option<(i32, i32)>,
        geometry: Rect,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurface::spawn_child_view");
        let view_tokens = ViewTokenPair::new().expect("failed to create token pair");
        let ViewRefPair { control_ref, view_ref } =
            ViewRefPair::new().expect("unable to create view ref pair");
        let view = View::new3(
            session.as_inner().clone(),
            view_tokens.view_token,
            control_ref,
            fuchsia_scenic::duplicate_view_ref(&view_ref)?,
            Some(String::from("Wayland Popup View")),
        );
        let parent = parent_ref.get(client)?;
        let parent_view = parent.view.clone();
        let root_surface_ref = parent.root_surface_ref();
        Self::add_child_view(parent_ref, client, view_tokens.view_holder_token)?;
        let xdg_surface = this.get(client)?;
        let surface_ref = xdg_surface.surface_ref();
        let task_queue = client.task_queue();
        let view_ptr = XdgSurfaceView::new(
            view,
            session,
            task_queue.clone(),
            this,
            surface_ref,
            parent_view,
            local_offset,
            geometry,
        )?;
        XdgSurfaceView::finish_setup_scene(&view_ptr, client)?;
        let xdg_surface = this.get_mut(client)?;
        xdg_surface.set_view(view_ptr.clone());
        xdg_surface.set_root_surface(root_surface_ref);
        Ok(())
    }

    fn handle_gfx_event(
        this: ObjectRef<Self>,
        event: fidl_fuchsia_ui_gfx::Event,
        task_queue: &TaskQueue,
    ) {
        ftrace::duration!("wayland", "XdgSurface::handle_gfx_event");
        match event {
            fidl_fuchsia_ui_gfx::Event::Metrics(fidl_fuchsia_ui_gfx::MetricsEvent {
                metrics: e,
                node_id: _,
            }) => task_queue.post(move |client| {
                if let Some(view) = this.get_mut(client)?.view.clone() {
                    view.lock().set_pixel_scale(client, e.scale_x, e.scale_y);
                }
                Ok(())
            }),
            fidl_fuchsia_ui_gfx::Event::ViewPropertiesChanged(
                fidl_fuchsia_ui_gfx::ViewPropertiesChangedEvent { properties, .. },
            ) => task_queue.post(move |client| {
                if let Some(view) = this.get_mut(client)?.view.clone() {
                    view.lock().handle_properies_changed(&properties);
                }
                Ok(())
            }),
            fidl_fuchsia_ui_gfx::Event::ViewDisconnected(
                fidl_fuchsia_ui_gfx::ViewDisconnectedEvent { view_holder_id },
            ) => task_queue.post(move |client| {
                if let Some(view) = this.get_mut(client)?.view.clone() {
                    view.lock().handle_view_disconnected(view_holder_id);
                }
                Ok(())
            }),

            e => println!("Got unhandled gfx event: {:?}", e),
        }
    }

    fn hit_test(
        this: ObjectRef<Self>,
        location_x: f32,
        location_y: f32,
        client: &Client,
    ) -> Option<(ObjectRef<Self>, ObjectRef<Surface>, (i32, i32))> {
        let mut maybe_xdg_surface_ref = Some(this);
        while let Some(xdg_surface_ref) = maybe_xdg_surface_ref.take() {
            if let Ok(xdg_surface) = xdg_surface_ref.get(client) {
                if let Some((parent_view, view_offset)) = xdg_surface.view.as_ref().map(|v| {
                    let view = v.lock();
                    (view.parent(), view.absolute_offset())
                }) {
                    let surface_ref = xdg_surface.surface_ref;
                    if let Ok(surface) = surface_ref.get(client) {
                        let pixel_scale = surface.pixel_scale();
                        let x = location_x * pixel_scale.0 - view_offset.0 as f32;
                        let y = location_y * pixel_scale.1 - view_offset.1 as f32;
                        if let Some((surface_ref, offset)) = surface.hit_test(x, y, client) {
                            let offset_x = offset.0 + view_offset.0;
                            let offset_y = offset.1 + view_offset.1;
                            return Some((xdg_surface_ref, surface_ref, (offset_x, offset_y)));
                        }
                    }
                    maybe_xdg_surface_ref = parent_view.as_ref().map(|v| v.lock().xdg_surface());
                }
            }
        }
        None
    }

    fn handle_input_events(
        this: ObjectRef<Self>,
        events: Vec<fidl_fuchsia_ui_input::InputEvent>,
        task_queue: &TaskQueue,
    ) {
        task_queue.post(move |client| {
            ftrace::duration!("wayland", "XdgSurface::handle_input_events");
            let source_xdg_surface = this.get(client)?;
            let source_surface_ref = source_xdg_surface.surface_ref;
            let pixel_scale = source_surface_ref.get(client)?.pixel_scale();
            for event in &events {
                // Hit-testing is used to determine the target for events with a location. Last
                // XDG surface is used for all other events.
                let target = if let Some((x, y)) = InputDispatcher::get_input_event_location(event)
                {
                    Self::hit_test(this, x, y, client)
                } else {
                    let xdg_surface_ref =
                        XdgSurface::get_event_target(source_surface_ref, client).unwrap_or(this);
                    Some((xdg_surface_ref, xdg_surface_ref.get(client)?.surface_ref, (0, 0)))
                };
                if let Some((xdg_surface_ref, surface_ref, offset)) = target {
                    if let Ok(surface) = surface_ref.get(client) {
                        let had_focus = client.input_dispatcher.has_focus(surface_ref);
                        // If the client has set window geometry we'll place the scenic
                        // surface at the (x,y) location specified in the window geometry.
                        //
                        // To compensate for this, we need to apply a translation to the
                        // pointer events received by scenic to adjust for this.
                        let (pointer_translation, pixel_scale) = {
                            let offset = {
                                (offset.0 as f32 / pixel_scale.0, offset.1 as f32 / pixel_scale.1)
                            };
                            let geometry = surface.window_geometry();
                            let translation =
                                (geometry.x as f32 - offset.0, geometry.y as f32 - offset.1);
                            (translation, pixel_scale)
                        };
                        client.input_dispatcher.handle_input_event(
                            source_surface_ref,
                            surface_ref,
                            &event,
                            pointer_translation,
                            pixel_scale,
                        )?;
                        let has_focus = client.input_dispatcher.has_focus(surface_ref);
                        if had_focus != has_focus {
                            // If our focus has changed we need to reconfigure so that the
                            // Activated flag can be set or cleared.
                            Self::configure(xdg_surface_ref, client)?;
                        }
                    }
                }
            }
            Ok(())
        });
    }

    fn handle_session_events(
        this: ObjectRef<Self>,
        events: Vec<fidl_fuchsia_ui_scenic::Event>,
        task_queue: TaskQueue,
    ) {
        ftrace::duration!("wayland", "XdgSurface::handle_session_events");
        let mut input_events = Vec::new();
        for event in events.into_iter() {
            match event {
                fidl_fuchsia_ui_scenic::Event::Input(e) => input_events.push(e),
                fidl_fuchsia_ui_scenic::Event::Gfx(e) => {
                    Self::handle_gfx_event(this, e, &task_queue)
                }
                fidl_fuchsia_ui_scenic::Event::Unhandled(c) => {
                    assert!(false, "Unhandled command {:?}", c)
                }
            }
        }

        if !input_events.is_empty() {
            Self::handle_input_events(this, input_events, &task_queue);
        }
    }

    fn spawn_session_listener(
        this: ObjectRef<Self>,
        client: &mut Client,
        server_end: ServerEnd<SessionListenerMarker>,
    ) -> Result<SessionListenerControlHandle, Error> {
        let task_queue = client.task_queue();
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
                            Self::handle_session_events(this, events, task_queue.clone());
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
}

#[cfg(not(feature = "flatland"))]
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
                this.get_mut(client)?.set_xdg_role(XdgSurfaceRole::Toplevel(toplevel_ref))?;
                let session_listener_control_handle =
                    XdgSurface::spawn_session_listener(this, client, server_end)?;
                let toplevel = toplevel_ref.get_mut(client)?;
                toplevel.session_listener_controller = Some(session_listener_control_handle);
            }
            ZxdgSurfaceV6Request::GetPopup { id, parent, positioner } => {
                let (client_end, server_end) = create_endpoints::<SessionListenerMarker>().unwrap();
                let session = client.display().create_session(Some(client_end))?;
                let popup = XdgPopup::new(this, client, session.clone(), positioner.into())?;
                let geometry = popup.geometry();
                let popup_ref = id.implement(client, popup)?;
                let xdg_surface = this.get_mut(client)?;
                xdg_surface.set_xdg_role(XdgSurfaceRole::Popup(popup_ref))?;
                XdgSurface::spawn_child_view(
                    this,
                    client,
                    session,
                    parent.into(),
                    Some((geometry.x, geometry.y)),
                    geometry,
                )?;
                let session_listener_control_handle =
                    XdgSurface::spawn_session_listener(this, client, server_end)?;
                let popup = popup_ref.get_mut(client)?;
                popup.session_listener_controller = Some(session_listener_control_handle);
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
    Popup(ObjectRef<XdgPopup>),
    Toplevel(ObjectRef<XdgToplevel>),
}

pub struct XdgPopup {
    /// A reference to the underlying wl_surface for this toplevel.
    surface_ref: ObjectRef<Surface>,
    /// A reference to the underlying xdg_surface for this toplevel.
    xdg_surface_ref: ObjectRef<XdgSurface>,
    /// This will be used to support reactive changes to positioner.
    #[allow(dead_code)]
    positioner_ref: ObjectRef<XdgPositioner>,
    /// Popup geometry.
    geometry: Rect,
    /// This handle can be used to terminate the |SessionListener| FIDL service
    /// associated with this toplevel.
    #[cfg(not(feature = "flatland"))]
    session_listener_controller: Option<SessionListenerControlHandle>,
}

impl XdgPopup {
    /// Performs a configure sequence for the XdgPopup object referenced by
    /// `this`.
    pub fn configure(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgPopup::configure");
        let geometry = this.get(client)?.geometry;
        client.event_queue().post(
            this.id(),
            ZxdgPopupV6Event::Configure {
                x: geometry.x,
                y: geometry.y,
                width: geometry.width,
                height: geometry.height,
            },
        )?;
        Ok(())
    }

    fn geometry(&self) -> Rect {
        self.geometry
    }

    fn close(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgPopup::close");
        client.event_queue().post(this.id(), ZxdgPopupV6Event::PopupDone)
    }
}

#[cfg(feature = "flatland")]
impl XdgPopup {
    /// Creates a new `XdgPopup` surface.
    pub fn new(
        xdg_surface_ref: ObjectRef<XdgSurface>,
        client: &mut Client,
        flatland: Flatland,
        positioner_ref: ObjectRef<XdgPositioner>,
    ) -> Result<Self, Error> {
        let geometry = positioner_ref.get(client)?.get_geometry()?;
        let surface_ref = xdg_surface_ref.get(client)?.surface_ref();
        surface_ref.get_mut(client)?.set_flatland(flatland)?;
        Ok(XdgPopup { surface_ref, xdg_surface_ref, positioner_ref, geometry })
    }

    pub fn shutdown(&self) {}
}

#[cfg(not(feature = "flatland"))]
impl XdgPopup {
    /// Creates a new `XdgPopup` surface.
    pub fn new(
        xdg_surface_ref: ObjectRef<XdgSurface>,
        client: &mut Client,
        session: ScenicSession,
        positioner_ref: ObjectRef<XdgPositioner>,
    ) -> Result<Self, Error> {
        let geometry = positioner_ref.get(client)?.get_geometry()?;
        let surface_ref = xdg_surface_ref.get(client)?.surface_ref();
        surface_ref.get_mut(client)?.set_session(session)?;
        Ok(XdgPopup {
            surface_ref,
            xdg_surface_ref,
            session_listener_controller: None,
            positioner_ref,
            geometry,
        })
    }

    pub fn shutdown(&self) {
        self.session_listener_controller.as_ref().map(|h| h.shutdown());
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
                let (surface_ref, xdg_surface_ref) = {
                    let popup = this.get(client)?;
                    (popup.surface_ref, popup.xdg_surface_ref)
                };
                xdg_surface_ref.get(client)?.shutdown(client);
                // We need to present here to commit the removal of our
                // popup. This will inform our parent that our view has
                // been destroyed.
                Surface::present(surface_ref, client, vec![])?;

                #[cfg(feature = "flatland")]
                surface_ref.get_mut(client)?.clear_flatland();
                #[cfg(not(feature = "flatland"))]
                surface_ref.get_mut(client)?.clear_session();
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
    /// This handle can be used to terminate the |ViewProvider| FIDL service
    /// associated with this toplevel.
    view_provider_controller: Option<ViewProviderControlHandle>,
    /// This proxy can be used to dismiss the |View| associated with this
    /// toplevel.
    view_controller_proxy: Option<ViewControllerProxy>,
    /// Identifier for the view.
    view_id: u32,
    /// This will be set to false after we received an initial commit.
    waiting_for_initial_commit: bool,
    /// A reference to an optional parent `XdgToplevel`.
    parent_ref: Option<ObjectRef<XdgToplevel>>,
    /// Optional title for `XdgToplevel`.
    title: Option<String>,
    /// This handle can be used to terminate the |SessionListener| FIDL service
    /// associated with this toplevel.
    #[cfg(not(feature = "flatland"))]
    session_listener_controller: Option<SessionListenerControlHandle>,
}

impl XdgToplevel {
    /// Performs a configure sequence for the XdgToplevel object referenced by
    /// `this`.
    pub fn configure(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgToplevel::configure");
        let (width, height, maximized, surface_ref) = {
            let (view, surface_ref, maybe_parent_ref) = {
                let toplevel = this.get(client)?;
                let xdg_surface_ref = toplevel.xdg_surface_ref;
                let xdg_surface = xdg_surface_ref.get(client)?;
                (xdg_surface.view.clone(), toplevel.surface_ref, toplevel.parent_ref)
            };
            // Let the client determine the size if it has a parent.
            let (width, height, maximized) = if maybe_parent_ref.is_some() {
                surface_ref
                    .get(client)
                    .map(|surface| {
                        let geometry = surface.window_geometry();
                        (geometry.width, geometry.height, false)
                    })
                    .unwrap_or((0, 0, false))
            } else {
                let display_info = client.display().display_info();
                let physical_size = view
                    .as_ref()
                    .map(|view| view.lock().physical_size())
                    .filter(|size| size.width != 0 && size.height != 0)
                    .unwrap_or(Size {
                        width: display_info.width_in_px as i32,
                        height: display_info.height_in_px as i32,
                    });
                (physical_size.width, physical_size.height, true)
            };
            (width, height, maximized, surface_ref)
        };

        let mut states = wl::Array::new();
        // If the surface doesn't have a parent, set the maximized state
        // to hint to the client it really should obey the geometry we're
        // asking for. From the xdg_shell spec:
        //
        // maximized:
        //    The surface is maximized. The window geometry specified in the
        //    configure event must be obeyed by the client.
        if maximized {
            states.push(zxdg_toplevel_v6::State::Maximized)?;
        }
        if client.input_dispatcher.has_focus(surface_ref) {
            // If the window has focus, we set the activated state. This is
            // just a hint to pass along to the client so it can draw itself
            // differently with and without focus.
            states.push(zxdg_toplevel_v6::State::Activated)?;
        }
        client
            .event_queue()
            .post(this.id(), ZxdgToplevelV6Event::Configure { width, height, states })?;

        Ok(())
    }

    /// Sets the parent for this `XdgToplevel`.
    pub fn set_parent(&mut self, parent: Option<ObjectRef<XdgToplevel>>) {
        self.parent_ref = parent;
    }

    /// Sets the title for this `XdgToplevel`.
    fn set_title(&mut self, title: Option<String>) {
        self.title = title;
    }

    fn close(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgToplevel::close");
        client.event_queue().post(this.id(), ZxdgToplevelV6Event::Close)
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
            view_provider_controller: None,
            view_controller_proxy: None,
            view_id: NEXT_VIEW_ID.fetch_add(1, Ordering::SeqCst) as u32,
            waiting_for_initial_commit: true,
            parent_ref: None,
            title: None,
        })
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
        let xdg_surface_ref = this.get(client)?.xdg_surface_ref;
        let task_queue = client.task_queue();
        let mut stream = server_end.into_stream().unwrap();
        let control_handle = stream.control_handle();
        fasync::Task::local(
            async move {
                while let Some(request) = stream.try_next().await.unwrap() {
                    match request {
                        ViewProviderRequest::CreateView2 { args, .. } => {
                            let mut view_creation_token = args.view_creation_token.unwrap();
                            let viewref_pair = ViewRefPair::new()?;
                            let view_ref =
                                fuchsia_scenic::duplicate_view_ref(&viewref_pair.view_ref)?;
                            let mut view_identity = ViewIdentityOnCreation::from(viewref_pair);
                            let (parent_viewport_watcher, parent_viewport_watcher_request) =
                                create_proxy::<ParentViewportWatcherMarker>()
                                    .expect("failed to create ParentViewportWatcherProxy");
                            let (view_ref_focused, view_ref_focused_request) =
                                create_proxy::<ViewRefFocusedMarker>()
                                    .expect("failed to create ViewRefFocusedProxy");
                            let view_bound_protocols = ViewBoundProtocols {
                                view_ref_focused: Some(view_ref_focused_request),
                                ..ViewBoundProtocols::EMPTY
                            };
                            flatland
                                .proxy()
                                .create_view2(
                                    &mut view_creation_token,
                                    &mut view_identity,
                                    view_bound_protocols,
                                    parent_viewport_watcher_request,
                                )
                                .expect("fidl error");
                            XdgSurface::spawn_keyboard_listener(
                                surface_ref,
                                view_ref,
                                task_queue.clone(),
                            )?;
                            XdgSurface::spawn_view_ref_focused_listener(
                                xdg_surface_ref,
                                surface_ref,
                                view_ref_focused,
                                task_queue.clone(),
                            );
                            XdgSurface::spawn_parent_viewport_listener(
                                xdg_surface_ref,
                                parent_viewport_watcher,
                                task_queue.clone(),
                            );
                            let view_ptr = XdgSurfaceView::new(
                                flatland,
                                task_queue.clone(),
                                xdg_surface_ref,
                                surface_ref,
                                None,
                                Some((0, 0)),
                                Rect { x: 0, y: 0, width: 0, height: 0 },
                            )?;
                            task_queue.post(move |client| {
                                XdgSurfaceView::finish_setup_scene(&view_ptr, client)?;
                                xdg_surface_ref.get_mut(client)?.set_view(view_ptr.clone());
                                Ok(())
                            });
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

    fn spawn_view(
        this: ObjectRef<Self>,
        client: &mut Client,
        flatland: Flatland,
    ) -> Result<ViewControllerProxy, Error> {
        ftrace::duration!("wayland", "XdgToplevel::spawn_view");
        let (proxy, server_end) = create_proxy::<ViewControllerMarker>()?;
        let stream = proxy.take_event_stream();
        let mut link_tokens = LinkTokenPair::new().expect("failed to create token pair");
        let viewref_pair = ViewRefPair::new()?;
        let view_ref_dup = fuchsia_scenic::duplicate_view_ref(&viewref_pair.view_ref)?;
        let view_ref_dup2 = fuchsia_scenic::duplicate_view_ref(&viewref_pair.view_ref)?;
        let mut view_identity = ViewIdentityOnCreation::from(viewref_pair);
        let toplevel = this.get(client)?;
        let annotations = toplevel.title.as_ref().map(|title| {
            let title_key = AnnotationKey {
                namespace: TITLE_ANNOTATION_NS.to_string(),
                value: TITLE_ANNOTATION_VALUE.to_string(),
            };
            vec![Annotation { key: title_key, value: AnnotationValue::Text(title.clone()) }]
        });
        let view_spec = ViewSpec {
            viewport_creation_token: Some(link_tokens.viewport_creation_token),
            view_ref: Some(view_ref_dup),
            annotations,
            ..ViewSpec::EMPTY
        };
        let (parent_viewport_watcher, parent_viewport_watcher_request) =
            create_proxy::<ParentViewportWatcherMarker>()
                .expect("failed to create ParentViewportWatcherProxy");
        let (view_ref_focused, view_ref_focused_request) =
            create_proxy::<ViewRefFocusedMarker>().expect("failed to create ViewRefFocusedProxy");
        let view_bound_protocols = ViewBoundProtocols {
            view_ref_focused: Some(view_ref_focused_request),
            ..ViewBoundProtocols::EMPTY
        };
        flatland
            .proxy()
            .create_view2(
                &mut link_tokens.view_creation_token,
                &mut view_identity,
                view_bound_protocols,
                parent_viewport_watcher_request,
            )
            .expect("fidl error");
        let xdg_surface_ref = toplevel.xdg_surface_ref;
        let surface_ref = toplevel.surface_ref;
        let task_queue = client.task_queue();
        XdgSurface::spawn_keyboard_listener(surface_ref, view_ref_dup2, task_queue.clone())?;
        XdgSurface::spawn_view_ref_focused_listener(
            xdg_surface_ref,
            surface_ref,
            view_ref_focused,
            task_queue.clone(),
        );
        XdgSurface::spawn_parent_viewport_listener(
            xdg_surface_ref,
            parent_viewport_watcher,
            task_queue.clone(),
        );
        let view_ptr = XdgSurfaceView::new(
            flatland,
            task_queue.clone(),
            xdg_surface_ref,
            surface_ref,
            None,
            Some((0, 0)),
            Rect { x: 0, y: 0, width: 0, height: 0 },
        )?;
        XdgSurfaceView::finish_setup_scene(&view_ptr, client)?;
        xdg_surface_ref.get_mut(client)?.set_view(view_ptr.clone());
        let graphical_presenter = client.display().graphical_presenter().clone();
        fasync::Task::local(
            async move {
                graphical_presenter
                    .present_view(view_spec, None, Some(server_end))
                    .await
                    .expect("failed to present view")
                    .unwrap_or_else(|e| println!("{:?}", e));

                // Wait for stream to close.
                let _ = stream.collect::<Vec<_>>().await;
                task_queue.post(move |client| {
                    XdgToplevel::close(this, client)?;
                    Ok(())
                });
                Ok(())
            }
            .unwrap_or_else(|e: Error| println!("{:?}", e)),
        )
        .detach();
        Ok(proxy)
    }

    pub fn finalize_commit(this: ObjectRef<Self>, client: &mut Client) -> Result<bool, Error> {
        ftrace::duration!("wayland", "XdgToplevel::finalize_commit");
        let top_level = this.get(client)?;
        // Initial commit requires that we spawn a view and send a configure event.
        if top_level.waiting_for_initial_commit {
            let xdg_surface_ref = top_level.xdg_surface_ref;
            let xdg_surface = xdg_surface_ref.get(client)?;
            let surface_ref = xdg_surface.surface_ref();
            let surface = surface_ref.get(client)?;
            let flatland = surface
                .flatland()
                .ok_or(format_err!("Unable to spawn view without a flatland instance"))?;

            // Spawn a child view if `XdgToplevel` has a parent or there's an existing
            // `XdgSurface` that can be used as parent.
            let maybe_parent_ref = if let Some(parent_ref) = top_level.parent_ref {
                let parent = parent_ref.get(client)?;
                Some(parent.xdg_surface_ref)
            } else {
                None
            };

            let (maybe_view_provider_control_handle, maybe_view_controller_proxy) = {
                if let Some(parent_ref) = maybe_parent_ref {
                    let offset = surface.offset();
                    let geometry = surface.window_geometry();
                    XdgSurface::spawn_child_view(
                        xdg_surface_ref,
                        client,
                        flatland.clone(),
                        parent_ref,
                        offset,
                        geometry,
                    )?;
                    (None, None)
                } else if client.take_view_provider_request() {
                    let control_handle =
                        XdgToplevel::spawn_view_provider(this, client, flatland.clone())?;
                    (Some(control_handle), None)
                } else {
                    let view_controller_proxy =
                        XdgToplevel::spawn_view(this, client, flatland.clone())?;
                    (None, Some(view_controller_proxy))
                }
            };

            // Initial commit requires that we send a configure event.
            let top_level = this.get_mut(client)?;
            top_level.waiting_for_initial_commit = false;
            top_level.view_provider_controller = maybe_view_provider_control_handle;
            top_level.view_controller_proxy = maybe_view_controller_proxy;
            // Maybe move keyboard focus to this XDG surface.
            if let Some(parent_ref) = maybe_parent_ref {
                let root_surface_ref = parent_ref.get(client)?.root_surface_ref();
                client
                    .input_dispatcher
                    .maybe_update_keyboard_focus(root_surface_ref, surface_ref)?;
            }
            XdgSurface::configure(xdg_surface_ref, client)?;
            client.xdg_surfaces.push(xdg_surface_ref);
        } else {
            let xdg_surface_ref = top_level.xdg_surface_ref;
            let xdg_surface = xdg_surface_ref.get(client)?;
            let surface = xdg_surface.surface_ref().get(client)?;
            let geometry = surface.window_geometry();
            let local_offset = surface.offset();
            if let Some(view) = xdg_surface.view.clone() {
                view.lock().set_geometry_and_local_offset(&geometry, &local_offset);
            }
        }
        Ok(true)
    }

    pub fn shutdown(&self, client: &Client) {
        if let Some(view_provider_controller) = self.view_provider_controller.as_ref() {
            view_provider_controller.shutdown();
            client.display().delete_view_provider(self.view_id);
        }
        if let Some(view_controller_proxy) = self.view_controller_proxy.as_ref() {
            view_controller_proxy.dismiss().unwrap_or_else(|e| println!("{:?}", e));
        }
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
            view_provider_controller: None,
            view_controller_proxy: None,
            view_id: NEXT_VIEW_ID.fetch_add(1, Ordering::SeqCst) as u32,
            waiting_for_initial_commit: true,
            parent_ref: None,
            title: None,
            session_listener_controller: None,
        })
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
        let xdg_surface_ref = this.get(client)?.xdg_surface_ref;
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
                            XdgSurface::spawn_keyboard_listener(
                                surface_ref,
                                view_ref,
                                task_queue.clone(),
                            )?;
                            let view_ptr = XdgSurfaceView::new(
                                view,
                                session.clone(),
                                task_queue.clone(),
                                xdg_surface_ref,
                                surface_ref,
                                None,
                                Some((0, 0)),
                                Rect { x: 0, y: 0, width: 0, height: 0 },
                            )?;
                            task_queue.post(move |client| {
                                XdgSurfaceView::finish_setup_scene(&view_ptr, client)?;
                                xdg_surface_ref.get_mut(client)?.set_view(view_ptr.clone());
                                Ok(())
                            });
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

    fn spawn_view(
        this: ObjectRef<Self>,
        client: &mut Client,
        session: ScenicSession,
    ) -> Result<ViewControllerProxy, Error> {
        ftrace::duration!("wayland", "XdgToplevel::spawn_view");
        let (proxy, server_end) = create_proxy::<ViewControllerMarker>()?;
        let stream = proxy.take_event_stream();
        let view_tokens = ViewTokenPair::new().expect("failed to create token pair");
        let ViewRefPair { control_ref, view_ref } =
            ViewRefPair::new().expect("unable to create view ref pair");
        let view_ref_dup = fuchsia_scenic::duplicate_view_ref(&view_ref)?;
        let toplevel = this.get(client)?;
        let annotations = toplevel.title.as_ref().map(|title| {
            let title_key = AnnotationKey {
                namespace: TITLE_ANNOTATION_NS.to_string(),
                value: TITLE_ANNOTATION_VALUE.to_string(),
            };
            vec![Annotation { key: title_key, value: AnnotationValue::Text(title.clone()) }]
        });
        let view_spec = ViewSpec {
            view_holder_token: Some(view_tokens.view_holder_token),
            view_ref: Some(view_ref_dup),
            annotations,
            ..ViewSpec::EMPTY
        };
        let view = View::new3(
            session.as_inner().clone(),
            view_tokens.view_token,
            control_ref,
            fuchsia_scenic::duplicate_view_ref(&view_ref)?,
            Some(String::from("Wayland View")),
        );
        let xdg_surface_ref = toplevel.xdg_surface_ref;
        let surface_ref = toplevel.surface_ref;
        let task_queue = client.task_queue();
        XdgSurface::spawn_keyboard_listener(surface_ref, view_ref, task_queue.clone())?;
        let view_ptr = XdgSurfaceView::new(
            view,
            session,
            task_queue.clone(),
            xdg_surface_ref,
            surface_ref,
            None,
            Some((0, 0)),
            Rect { x: 0, y: 0, width: 0, height: 0 },
        )?;
        XdgSurfaceView::finish_setup_scene(&view_ptr, client)?;
        xdg_surface_ref.get_mut(client)?.set_view(view_ptr.clone());
        let graphical_presenter = client.display().graphical_presenter().clone();
        fasync::Task::local(
            async move {
                graphical_presenter
                    .present_view(view_spec, None, Some(server_end))
                    .await
                    .expect("failed to present view")
                    .unwrap_or_else(|e| println!("{:?}", e));

                // Wait for stream to close.
                let _ = stream.collect::<Vec<_>>().await;
                task_queue.post(move |client| {
                    XdgToplevel::close(this, client)?;
                    Ok(())
                });
                Ok(())
            }
            .unwrap_or_else(|e: Error| println!("{:?}", e)),
        )
        .detach();
        Ok(proxy)
    }

    pub fn finalize_commit(this: ObjectRef<Self>, client: &mut Client) -> Result<bool, Error> {
        ftrace::duration!("wayland", "XdgToplevel::finalize_commit");
        let top_level = this.get(client)?;
        // Initial commit requires that we spawn a view and send a configure event.
        if top_level.waiting_for_initial_commit {
            let xdg_surface_ref = top_level.xdg_surface_ref;
            let xdg_surface = xdg_surface_ref.get(client)?;
            let surface_ref = xdg_surface.surface_ref();
            let surface = surface_ref.get(client)?;
            let session =
                surface.session().ok_or(format_err!("Unable to spawn view without a session."))?;

            // Spawn a child view if `XdgToplevel` has a parent or there's an existing
            // `XdgSurface` that can be used as parent.
            let maybe_parent_ref = if let Some(parent_ref) = top_level.parent_ref {
                let parent = parent_ref.get(client)?;
                Some(parent.xdg_surface_ref)
            } else {
                None
            };

            let (maybe_view_provider_control_handle, maybe_view_controller_proxy) = {
                if let Some(parent_ref) = maybe_parent_ref {
                    let offset = surface.offset();
                    let geometry = surface.window_geometry();
                    XdgSurface::spawn_child_view(
                        xdg_surface_ref,
                        client,
                        session.clone(),
                        parent_ref,
                        offset,
                        geometry,
                    )?;
                    (None, None)
                } else if client.take_view_provider_request() {
                    let control_handle =
                        XdgToplevel::spawn_view_provider(this, client, session.clone())?;
                    (Some(control_handle), None)
                } else {
                    let view_controller_proxy =
                        XdgToplevel::spawn_view(this, client, session.clone())?;
                    (None, Some(view_controller_proxy))
                }
            };

            // Initial commit requires that we send a configure event.
            let top_level = this.get_mut(client)?;
            top_level.waiting_for_initial_commit = false;
            top_level.view_provider_controller = maybe_view_provider_control_handle;
            top_level.view_controller_proxy = maybe_view_controller_proxy;
            // Maybe move keyboard focus to this XDG surface.
            if let Some(parent_ref) = maybe_parent_ref {
                let root_surface_ref = parent_ref.get(client)?.root_surface_ref();
                client
                    .input_dispatcher
                    .maybe_update_keyboard_focus(root_surface_ref, surface_ref)?;
            }
            XdgSurface::configure(xdg_surface_ref, client)?;
            client.xdg_surfaces.push(xdg_surface_ref);
        } else {
            let xdg_surface_ref = top_level.xdg_surface_ref;
            let xdg_surface = xdg_surface_ref.get(client)?;
            let surface = xdg_surface.surface_ref().get(client)?;
            let geometry = surface.window_geometry();
            let local_offset = surface.offset();
            if let Some(view) = xdg_surface.view.clone() {
                view.lock().set_geometry_and_local_offset(&geometry, &local_offset);
            }
        }
        Ok(true)
    }

    pub fn shutdown(&self, client: &Client) {
        if let Some(view_provider_controller) = self.view_provider_controller.as_ref() {
            view_provider_controller.shutdown();
            client.display().delete_view_provider(self.view_id);
        }
        if let Some(view_controller_proxy) = self.view_controller_proxy.as_ref() {
            view_controller_proxy.dismiss().unwrap_or_else(|e| println!("{:?}", e));
        }
        self.session_listener_controller.as_ref().map(|h| h.shutdown());
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
                let (surface_ref, xdg_surface_ref) = {
                    let toplevel = this.get(client)?;
                    (toplevel.surface_ref, toplevel.xdg_surface_ref)
                };
                client.xdg_surfaces.retain(|&x| x != xdg_surface_ref);
                let xdg_surface = xdg_surface_ref.get(client)?;
                xdg_surface.shutdown(client);
                if client.input_dispatcher.has_focus(surface_ref) {
                    // Move keyboard focus to new event target.
                    let source_surface_ref = xdg_surface.root_surface_ref();
                    let maybe_target = XdgSurface::get_event_target(source_surface_ref, client);
                    if let Some(target_xdg_surface_ref) = maybe_target {
                        let target_surface_ref = target_xdg_surface_ref.get(client)?.surface_ref;
                        client
                            .input_dispatcher
                            .maybe_update_keyboard_focus(source_surface_ref, target_surface_ref)?;
                    }
                }
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
            ZxdgToplevelV6Request::SetParent { parent } => {
                let toplevel = this.get_mut(client)?;
                let maybe_parent = if parent != 0 { Some(parent.into()) } else { None };
                toplevel.set_parent(maybe_parent);
            }
            ZxdgToplevelV6Request::SetTitle { title } => {
                let toplevel = this.get_mut(client)?;
                toplevel.set_title(Some(title));
            }
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

/// A scenic view implementation to back an |XdgSurface| resource.
///
/// An `XdgSurfaceView` will be created by the `ViewProvider` for an
/// `XdgSurface`.
struct XdgSurfaceView {
    #[cfg(feature = "flatland")]
    flatland: Flatland,
    #[cfg(feature = "flatland")]
    transform: Option<TransformId>,
    #[cfg(not(feature = "flatland"))]
    view: Option<View>,
    #[cfg(not(feature = "flatland"))]
    session: ScenicSession,
    #[cfg(not(feature = "flatland"))]
    background_node: ShapeNode,
    #[cfg(not(feature = "flatland"))]
    container_node: EntityNode,
    logical_size: SizeF,
    #[cfg(not(feature = "flatland"))]
    pixel_scale: (f32, f32),
    local_offset: Option<(i32, i32)>,
    absolute_offset: (i32, i32),
    task_queue: TaskQueue,
    xdg_surface: ObjectRef<XdgSurface>,
    surface: ObjectRef<Surface>,
    geometry: Rect,
    parent: Option<XdgSurfaceViewPtr>,
    #[cfg(feature = "flatland")]
    children: BTreeSet<u64>,
    #[cfg(not(feature = "flatland"))]
    children: BTreeMap<u32, (ViewHolder, EntityNode)>,
}

type XdgSurfaceViewPtr = Arc<Mutex<XdgSurfaceView>>;

impl XdgSurfaceView {
    fn present_internal(&mut self) {
        let surface_ref = self.surface;
        self.task_queue.post(move |client| Surface::present(surface_ref, client, vec![]));
    }

    fn update_and_present(&mut self) {
        self.update();
        self.present_internal();
    }

    fn reconfigure(&self) {
        // If we have both a size and a pixel scale, we're ready to send the
        // configure event to the client. We need both because we send expose
        // physical pixels to the client.
        if self.logical_size.width != 0.0 && self.logical_size.width != 0.0 {
            // Post the xdg_toplevel::configure event to inform the client about
            // the change.
            let xdg_surface = self.xdg_surface;
            self.task_queue.post(move |client| XdgSurface::configure(xdg_surface, client))
        }
    }

    fn compute_absolute_offset(
        parent: &Option<XdgSurfaceViewPtr>,
        physical_size: &Size,
        local_offset: &Option<(i32, i32)>,
        geometry: &Rect,
    ) -> (i32, i32) {
        // Allow a non-zero absolute offset if we have a parent view.
        parent.as_ref().map_or((0, 0), |parent| {
            // Center in available space by default and relative to parent if
            // local offset is set.
            local_offset.map_or_else(
                || {
                    if physical_size.width != 0 && physical_size.width != 0 {
                        (
                            (physical_size.width as i32 - geometry.width) / 2,
                            (physical_size.height as i32 - geometry.height) / 2,
                        )
                    } else {
                        (0, 0)
                    }
                },
                |(x, y)| {
                    let parent_offset = parent.lock().absolute_offset();
                    (parent_offset.0 + x, parent_offset.1 + y)
                },
            )
        })
    }

    fn update_absolute_offset(&mut self) {
        self.absolute_offset = Self::compute_absolute_offset(
            &self.parent,
            &self.physical_size(),
            &self.local_offset,
            &self.geometry,
        );
    }

    fn absolute_offset(&self) -> (i32, i32) {
        self.absolute_offset
    }
}

#[cfg(feature = "flatland")]
impl XdgSurfaceView {
    pub fn new(
        flatland: Flatland,
        task_queue: TaskQueue,
        xdg_surface: ObjectRef<XdgSurface>,
        surface: ObjectRef<Surface>,
        parent: Option<XdgSurfaceViewPtr>,
        local_offset: Option<(i32, i32)>,
        geometry: Rect,
    ) -> Result<XdgSurfaceViewPtr, Error> {
        // Get initial size from parent if available.
        let logical_size = parent
            .as_ref()
            .map_or(SizeF { width: 0.0, height: 0.0 }, |parent| parent.lock().logical_size);
        let physical_size = Self::physical_size_internal(&logical_size);
        let absolute_offset =
            Self::compute_absolute_offset(&parent, &physical_size, &local_offset, &geometry);
        let transform = flatland.alloc_transform_id();
        flatland.proxy().create_transform(&mut transform.clone()).expect("fidl error");
        let view_controller = XdgSurfaceView {
            flatland,
            transform: Some(transform),
            logical_size,
            local_offset,
            absolute_offset,
            task_queue,
            xdg_surface,
            surface,
            geometry,
            parent,
            children: BTreeSet::new(),
        };
        let view_controller = Arc::new(Mutex::new(view_controller));
        Ok(view_controller)
    }

    pub fn finish_setup_scene(
        view_controller: &XdgSurfaceViewPtr,
        client: &mut Client,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurfaceView::finish_setup_scene");
        let mut vc = view_controller.lock();
        vc.setup_scene();
        vc.attach(vc.surface, client)?;

        // Perform an update if we have an initial size.
        if vc.logical_size.width != 0.0 && vc.logical_size.width != 0.0 {
            vc.update();
        }
        vc.present_internal();
        Ok(())
    }

    pub fn shutdown(&mut self) {
        self.transform = None;
    }

    fn physical_size_internal(logical_size: &SizeF) -> Size {
        Size {
            width: logical_size.width.round() as i32,
            height: logical_size.height.round() as i32,
        }
    }

    pub fn physical_size(&self) -> Size {
        Self::physical_size_internal(&self.logical_size)
    }

    fn attach(&self, surface: ObjectRef<Surface>, client: &Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurfaceView::attach");
        let surface = surface.get(client)?;
        let surface_transform = surface.transform().expect("surface is missing a transform");
        self.transform.as_ref().map(|transform| {
            self.flatland
                .proxy()
                .add_child(&mut transform.clone(), &mut surface_transform.clone())
                .expect("fidl error");
        });
        Ok(())
    }

    fn setup_scene(&self) {
        ftrace::duration!("wayland", "XdgSurfaceView::setup_scene");
        self.transform.as_ref().map(|transform| {
            self.flatland.proxy().set_root_transform(&mut transform.clone()).expect("fidl error");
        });
    }

    fn update(&mut self) {
        ftrace::duration!("wayland", "XdgSurfaceView::update");
        self.transform.as_ref().map(|transform| {
            let mut translation = Vec_ { x: self.absolute_offset.0, y: self.absolute_offset.1 };
            self.flatland
                .proxy()
                .set_translation(&mut transform.clone(), &mut translation)
                .expect("fidl error");
        });
    }

    pub fn handle_layout_changed(&mut self, logical_size: &SizeF) {
        ftrace::duration!("wayland", "XdgSurfaceView::handle_layout_changed");
        if *logical_size != self.logical_size {
            self.logical_size = *logical_size;
            for id in &self.children {
                self.set_viewport_properties(*id);
            }
            self.update_absolute_offset();
            self.update_and_present();
            self.reconfigure();
        }
    }

    pub fn set_geometry_and_local_offset(
        &mut self,
        geometry: &Rect,
        local_offset: &Option<(i32, i32)>,
    ) {
        ftrace::duration!("wayland", "XdgSurfaceView::set_geometry_and_local_offset");
        self.geometry = *geometry;
        self.local_offset = *local_offset;
        let absolute_offset = Self::compute_absolute_offset(
            &self.parent,
            &self.physical_size(),
            &self.local_offset,
            &self.geometry,
        );
        if absolute_offset != self.absolute_offset {
            self.absolute_offset = absolute_offset;
            self.update_and_present();
        }
    }

    pub fn add_child_view(
        &mut self,
        id: u64,
        mut viewport_creation_token: ViewportCreationToken,
        server_end: ServerEnd<ChildViewWatcherMarker>,
    ) {
        ftrace::duration!("wayland", "XdgSurfaceView::add_child_view");
        let viewport_properties = ViewportProperties {
            logical_size: Some(SizeU {
                width: self.logical_size.width.round() as u32,
                height: self.logical_size.height.round() as u32,
            }),
            ..ViewportProperties::EMPTY
        };
        let mut child_transform = TransformId { value: id.into() };
        let mut link = ContentId { value: id.into() };
        self.flatland.proxy().create_transform(&mut child_transform).expect("fidl error");
        self.flatland
            .proxy()
            .create_viewport(
                &mut link,
                &mut viewport_creation_token,
                viewport_properties,
                server_end,
            )
            .expect("fidl error");
        self.flatland.proxy().set_content(&mut child_transform, &mut link).expect("fidl error");
        self.transform.as_ref().map(|transform| {
            self.flatland
                .proxy()
                .add_child(&mut transform.clone(), &mut child_transform)
                .expect("fidl error");
        });
        self.children.insert(id);
        self.update_and_present();
    }

    pub fn handle_view_disconnected(&mut self, id: u64) {
        ftrace::duration!("wayland", "XdgSurfaceView::handle_view_disconnected");
        if self.children.remove(&id) {
            self.transform.as_ref().map(|transform| {
                let mut child_transform = TransformId { value: id.into() };
                self.flatland
                    .proxy()
                    .remove_child(&mut transform.clone(), &mut child_transform)
                    .expect("fidl error");
                self.flatland.proxy().release_transform(&mut child_transform).expect("fidl error");
                let mut link = ContentId { value: id.into() };
                let _ = self.flatland.proxy().release_viewport(&mut link);
            });
        }
        self.update_and_present();
    }

    fn set_viewport_properties(&self, id: u64) {
        let viewport_properties = ViewportProperties {
            logical_size: Some(SizeU {
                width: self.logical_size.width.round() as u32,
                height: self.logical_size.height.round() as u32,
            }),
            ..ViewportProperties::EMPTY
        };
        let mut link = ContentId { value: id.into() };
        self.flatland
            .proxy()
            .set_viewport_properties(&mut link, viewport_properties)
            .expect("fidl error");
    }
}

#[cfg(not(feature = "flatland"))]
impl XdgSurfaceView {
    pub fn new(
        view: View,
        session: ScenicSession,
        task_queue: TaskQueue,
        xdg_surface: ObjectRef<XdgSurface>,
        surface: ObjectRef<Surface>,
        parent: Option<XdgSurfaceViewPtr>,
        local_offset: Option<(i32, i32)>,
        geometry: Rect,
    ) -> Result<XdgSurfaceViewPtr, Error> {
        // Get initial size and pixel scale from parent if available.
        let (logical_size, pixel_scale) =
            parent.as_ref().map_or((SizeF { width: 0.0, height: 0.0 }, (1.0, 1.0)), |parent| {
                let parent = parent.lock();
                (parent.logical_size, parent.pixel_scale)
            });
        let physical_size = Self::physical_size_internal(&logical_size, &pixel_scale);
        let absolute_offset =
            Self::compute_absolute_offset(&parent, &physical_size, &local_offset, &geometry);
        let view_controller = XdgSurfaceView {
            view: Some(view),
            session: session.clone(),
            background_node: ShapeNode::new(session.as_inner().clone()),
            container_node: EntityNode::new(session.as_inner().clone()),
            logical_size,
            pixel_scale,
            local_offset,
            absolute_offset,
            task_queue,
            xdg_surface,
            surface,
            geometry,
            parent,
            children: BTreeMap::new(),
        };
        let view_controller = Arc::new(Mutex::new(view_controller));
        Ok(view_controller)
    }

    pub fn finish_setup_scene(
        view_controller: &XdgSurfaceViewPtr,
        client: &mut Client,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurfaceView::finish_setup_scene");
        let mut vc = view_controller.lock();
        vc.setup_scene();
        vc.attach(vc.surface, client)?;

        vc.surface.get_mut(client)?.set_pixel_scale(vc.pixel_scale.0, vc.pixel_scale.1);

        // Perform an update if we have an initial size.
        if vc.logical_size.width != 0.0 && vc.logical_size.width != 0.0 {
            vc.update();
        }
        vc.present_internal();
        Ok(())
    }

    pub fn shutdown(&mut self) {
        self.view = None;
    }

    fn physical_size_internal(logical_size: &SizeF, pixel_scale: &(f32, f32)) -> Size {
        Size {
            width: (logical_size.width * pixel_scale.0).round() as i32,
            height: (logical_size.height * pixel_scale.1).round() as i32,
        }
    }

    pub fn physical_size(&self) -> Size {
        Self::physical_size_internal(&self.logical_size, &self.pixel_scale)
    }

    fn attach(&self, surface: ObjectRef<Surface>, client: &Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgSurfaceView::attach");
        let surface = surface.get(client)?;
        let node = surface.node().expect("surface is missing a node");
        self.container_node.add_child(node);
        Ok(())
    }

    fn setup_scene(&self) {
        ftrace::duration!("wayland", "XdgSurfaceView::setup_scene");
        self.view.as_ref().map(|v| {
            v.add_child(&self.background_node);
            v.add_child(&self.container_node);
        });

        self.container_node.resource().set_event_mask(gfx::METRICS_EVENT_MASK);

        let material = Material::new(self.session.as_inner().clone());
        material.set_color(ColorRgba { red: 0x0, green: 0x0, blue: 0x0, alpha: 0x0 });
        // To debug child views:
        // material.set_color(ColorRgba { red: 0x0, green: 0xff, blue: 0x0, alpha: 0x40 });
        self.background_node.set_material(&material);
    }

    fn update(&mut self) {
        ftrace::duration!("wayland", "XdgSurfaceView::update");
        let center_x = self.logical_size.width * 0.5;
        let center_y = self.logical_size.height * 0.5;
        self.background_node.set_shape(&Rectangle::new(
            self.session.as_inner().clone(),
            self.logical_size.width,
            self.logical_size.height,
        ));
        // Place the container node above the background.
        self.background_node.set_translation(center_x, center_y, 0.0);
        self.container_node.set_translation(
            self.absolute_offset.0 as f32 / self.pixel_scale.0,
            self.absolute_offset.1 as f32 / self.pixel_scale.1,
            -1.0,
        );
        // Update all child views.
        let child_z_delta = 1.0 / self.children.len() as f32;
        let mut child_z = -2.0;
        for (_, node) in self.children.values() {
            node.set_translation(0.0, 0.0, child_z);
            child_z -= child_z_delta;
        }
    }

    pub fn set_pixel_scale(&mut self, client: &mut Client, scale_x: f32, scale_y: f32) {
        ftrace::duration!("wayland", "XdgSurfaceView::set_pixel_scale");
        let pixel_scale = (scale_x, scale_y);
        if pixel_scale != self.pixel_scale {
            self.pixel_scale = pixel_scale;
            if let Ok(surface) = self.surface.get_mut(client) {
                surface.set_pixel_scale(scale_x, scale_y);
            }
            self.update_and_present();
            self.reconfigure();
        }
    }

    pub fn set_geometry_and_local_offset(
        &mut self,
        geometry: &Rect,
        local_offset: &Option<(i32, i32)>,
    ) {
        ftrace::duration!("wayland", "XdgSurfaceView::set_geometry_and_local_offset");
        self.geometry = *geometry;
        self.local_offset = *local_offset;
        let absolute_offset = Self::compute_absolute_offset(
            &self.parent,
            &self.physical_size(),
            &self.local_offset,
            &self.geometry,
        );
        if absolute_offset != self.absolute_offset {
            self.absolute_offset = absolute_offset;
            self.update_and_present();
        }
    }

    pub fn handle_properies_changed(&mut self, properties: &fidl_fuchsia_ui_gfx::ViewProperties) {
        ftrace::duration!("wayland", "XdgSurfaceView::handle_properies_changed");
        let width = properties.bounding_box.max.x - properties.bounding_box.min.x;
        let height = properties.bounding_box.max.y - properties.bounding_box.min.y;
        let logical_size = SizeF { width, height };
        if logical_size != self.logical_size {
            self.logical_size = logical_size;
            for (view_holder, _) in self.children.values() {
                self.set_view_properties(view_holder);
            }
            self.update_absolute_offset();
            self.update_and_present();
            self.reconfigure();
        }
    }

    pub fn add_child_view(&mut self, view_holder: ViewHolder) {
        ftrace::duration!("wayland", "XdgSurfaceView::add_child_view");
        self.set_view_properties(&view_holder);
        let node = EntityNode::new(self.session.as_inner().clone());
        node.attach(&view_holder);
        self.view.as_ref().map(|v| {
            v.add_child(&node);
        });
        let id = view_holder.id();
        self.children.insert(id, (view_holder, node));
        self.update_and_present();
    }

    pub fn handle_view_disconnected(&mut self, view_holder_id: u32) {
        ftrace::duration!("wayland", "XdgSurfaceView::handle_view_disconnected");
        if let Some(value) = self.children.remove(&view_holder_id) {
            self.view.as_ref().map(|v| {
                v.detach_child(&value.1);
            });
        }
        self.update_and_present();
    }

    fn set_view_properties(&self, view_holder: &ViewHolder) {
        let view_properties = gfx::ViewProperties {
            bounding_box: gfx::BoundingBox {
                min: gfx::Vec3 { x: 0.0, y: 0.0, z: -5.0 },
                max: gfx::Vec3 { x: self.logical_size.width, y: self.logical_size.height, z: 0.0 },
            },
            downward_input: true,
            focus_change: false,
            inset_from_min: gfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
            inset_from_max: gfx::Vec3 { x: 0.0, y: 0.0, z: 0.0 },
        };
        view_holder.set_view_properties(view_properties);
    }

    fn parent(&self) -> Option<XdgSurfaceViewPtr> {
        self.parent.clone()
    }

    fn xdg_surface(&self) -> ObjectRef<XdgSurface> {
        self.xdg_surface
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn positioner_default() -> Result<(), Error> {
        let positioner = XdgPositioner::new();
        assert_eq!(Rect { x: 0, y: 0, width: 0, height: 0 }, positioner.get_geometry()?);
        Ok(())
    }

    #[test]
    fn positioner_set_offset_and_size() -> Result<(), Error> {
        let mut positioner = XdgPositioner::new();
        positioner.set_offset(250, 550);
        positioner.set_size(100, 200);
        assert_eq!(Rect { x: 200, y: 450, width: 100, height: 200 }, positioner.get_geometry()?);
        Ok(())
    }

    #[test]
    fn positioner_set_anchor_rect() -> Result<(), Error> {
        let mut positioner = XdgPositioner::new();
        positioner.set_offset(0, 0);
        positioner.set_size(168, 286);
        positioner.set_anchor_rect(486, 0, 44, 28);
        positioner.set_anchor(Enum::Recognized(Anchor::Bottom | Anchor::Left));
        positioner.set_gravity(Enum::Recognized(Gravity::Bottom | Gravity::Right));
        assert_eq!(Rect { x: 486, y: 28, width: 168, height: 286 }, positioner.get_geometry()?);
        Ok(())
    }
}
