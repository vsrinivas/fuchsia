// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::{Client, TaskQueue},
    crate::compositor::{Surface, SurfaceCommand, SurfaceRole},
    crate::display::Callback,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    crate::scenic::{Flatland, FlatlandPtr},
    anyhow::{format_err, Error},
    async_utils::hanging_get::client::HangingGetStream,
    fidl::endpoints::{create_endpoints, create_proxy, create_request_stream, ServerEnd},
    fidl::prelude::*,
    fidl_fuchsia_element::{
        Annotation, AnnotationKey, AnnotationValue, ViewControllerMarker, ViewControllerProxy,
        ViewSpec,
    },
    fidl_fuchsia_math::{Rect, Size, SizeF},
    fidl_fuchsia_math::{SizeU, Vec_},
    fidl_fuchsia_ui_app::{ViewProviderControlHandle, ViewProviderMarker, ViewProviderRequest},
    fidl_fuchsia_ui_composition::{
        ChildViewWatcherMarker, ChildViewWatcherProxy, ContentId, FlatlandEvent,
        FlatlandEventStream, FlatlandMarker, ParentViewportWatcherMarker,
        ParentViewportWatcherProxy, TransformId, ViewportProperties,
    },
    fidl_fuchsia_ui_pointer::{
        MousePointerSample, MouseSourceMarker, MouseSourceProxy, TouchPointerSample,
        TouchSourceMarker, TouchSourceProxy,
    },
    fidl_fuchsia_ui_views::ViewRef,
    fidl_fuchsia_ui_views::{
        ViewIdentityOnCreation, ViewRefFocusedMarker, ViewRefFocusedProxy, ViewportCreationToken,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_scenic::flatland::{LinkTokenPair, ViewBoundProtocols},
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
    std::{collections::BTreeSet, convert::TryInto},
    xdg_shell_server_protocol::{
        self as xdg_shell,
        xdg_positioner::{Anchor, Gravity},
        xdg_toplevel, XdgPopupEvent, XdgPopupRequest, XdgPositionerRequest, XdgSurfaceEvent,
        XdgSurfaceRequest, XdgToplevelEvent, XdgToplevelRequest, XdgWmBase, XdgWmBaseRequest,
    },
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

/// `XdgShell` is an implementation of the xdg_wm_base global.
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

impl RequestReceiver<XdgWmBase> for XdgShell {
    fn receive(
        this: ObjectRef<Self>,
        request: XdgWmBaseRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            XdgWmBaseRequest::Destroy => {
                client.delete_id(this.id())?;
            }
            XdgWmBaseRequest::GetXdgSurface { id, surface } => {
                let xdg_surface = XdgSurface::new(surface);
                let surface_ref = xdg_surface.surface_ref;
                let xdg_surface_ref = id.implement(client, xdg_surface)?;
                surface_ref.get_mut(client)?.set_role(SurfaceRole::XdgSurface(xdg_surface_ref))?;
            }
            XdgWmBaseRequest::CreatePositioner { id } => {
                id.implement(client, XdgPositioner::new())?;
            }
            XdgWmBaseRequest::Pong { .. } => {}
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

        let anchor = self.anchor.as_enum()?;
        geometry.x += match anchor {
            Anchor::Left | Anchor::BottomLeft | Anchor::TopLeft => self.anchor_rect.x,
            Anchor::Right | Anchor::BottomRight | Anchor::TopRight => {
                self.anchor_rect.x + self.anchor_rect.width
            }
            _ => self.anchor_rect.x + self.anchor_rect.width / 2,
        };

        geometry.y += match anchor {
            Anchor::Top | Anchor::TopLeft | Anchor::TopRight => self.anchor_rect.y,
            Anchor::Bottom | Anchor::BottomLeft | Anchor::BottomRight => {
                self.anchor_rect.y + self.anchor_rect.height
            }
            _ => self.anchor_rect.y + self.anchor_rect.height / 2,
        };

        let gravity = self.gravity.as_enum()?;
        geometry.x -= match gravity {
            Gravity::Left | Gravity::BottomLeft | Gravity::TopLeft => geometry.width,
            Gravity::Right | Gravity::BottomRight | Gravity::TopRight => 0,
            _ => geometry.width / 2,
        };

        geometry.y -= match gravity {
            Gravity::Top | Gravity::TopLeft | Gravity::TopRight => geometry.height,
            Gravity::Bottom | Gravity::BottomLeft | Gravity::BottomRight => 0,
            _ => geometry.height / 2,
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

impl RequestReceiver<xdg_shell::XdgPositioner> for XdgPositioner {
    fn receive(
        this: ObjectRef<Self>,
        request: XdgPositionerRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            XdgPositionerRequest::Destroy => {
                client.delete_id(this.id())?;
            }
            XdgPositionerRequest::SetSize { width, height } => {
                if width <= 0 || height <= 0 {
                    return Err(format_err!(
                        "invalid_input error width={:?} height={:?}",
                        width,
                        height
                    ));
                }
                this.get_mut(client)?.set_size(width, height);
            }
            XdgPositionerRequest::SetAnchorRect { x, y, width, height } => {
                if width <= 0 || height <= 0 {
                    return Err(format_err!(
                        "invalid_input error width={:?} height={:?}",
                        width,
                        height
                    ));
                }
                this.get_mut(client)?.set_anchor_rect(x, y, width, height);
            }
            XdgPositionerRequest::SetAnchor { anchor } => {
                this.get_mut(client)?.set_anchor(anchor);
            }
            XdgPositionerRequest::SetGravity { gravity } => {
                this.get_mut(client)?.set_gravity(gravity);
            }
            XdgPositionerRequest::SetConstraintAdjustment { .. } => {}
            XdgPositionerRequest::SetOffset { x, y } => {
                this.get_mut(client)?.set_offset(x, y);
            }
            XdgPositionerRequest::SetReactive { .. } => {}
            XdgPositionerRequest::SetParentSize { .. } => {}
            XdgPositionerRequest::SetParentConfigure { .. } => {}
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
        if let Some(xdg_surface) = this.try_get(client) {
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
            client.event_queue().post(this.id(), XdgSurfaceEvent::Configure { serial })?;
        }
        Ok(())
    }

    /// Handle a commit request to this `XdgSurface`.
    ///
    /// This will be triggered by a wl_surface::commit request to the backing
    /// wl_surface object for this xdg_surface, and simply delegates the request
    /// to the concrete surface.
    pub fn finalize_commit(this: ObjectRef<Self>, client: &mut Client) -> Result<bool, Error> {
        ftrace::duration!("wayland", "XdgSurface::finalize_commit");
        if let Some(xdg_surface) = this.try_get(client) {
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
                if let Some(popup) = popup.try_get(client) {
                    popup.shutdown();
                }
            }
            Some(XdgSurfaceRole::Toplevel(toplevel)) => {
                if let Some(toplevel) = toplevel.try_get(client) {
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
            if let Some(xdg_surface) = xdg_surface_ref.try_get(client) {
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
        let transform = flatland.borrow_mut().alloc_transform_id();
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
        flatland: FlatlandPtr,
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
            .borrow()
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
        client.xdg_surfaces.push(this);
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
                                if let Some(surface) = surface_ref.try_get_mut(client) {
                                    let callbacks = surface.take_on_next_frame_begin_callbacks();
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
                        if let Some(logical_size) = layout_info
                            .logical_size
                            // TODO(https://fxbug.dev/91259): Remove this filter when
                            // no longer needed.
                            .filter(|size| size.width > 32 || size.height > 32)
                            .map(|size| SizeF {
                                width: size.width as f32,
                                height: size.height as f32,
                            })
                        {
                            task_queue.post(move |client| {
                                if let Some(view) = this.get(client)?.view.clone() {
                                    view.lock().handle_layout_changed(&logical_size);
                                }
                                Ok(())
                            });
                        }
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
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

    fn spawn_touch_listener(
        this: ObjectRef<Self>,
        touch_source: TouchSourceProxy,
        task_queue: TaskQueue,
    ) {
        fasync::Task::local(async move {
            let mut responses: Vec<fidl_fuchsia_ui_pointer::TouchResponse> = Vec::new();
            loop {
                let result = touch_source.watch(&mut responses.into_iter()).await;
                match result {
                    Ok(returned_events) => {
                        responses = returned_events
                            .iter()
                            .map(|event| fidl_fuchsia_ui_pointer::TouchResponse {
                                response_type: event.pointer_sample.as_ref().and_then(|_| {
                                    Some(fidl_fuchsia_ui_pointer::TouchResponseType::Yes)
                                }),
                                ..fidl_fuchsia_ui_pointer::TouchResponse::EMPTY
                            })
                            .collect();
                        let events = returned_events.clone();
                        task_queue.post(move |client| {
                            if let Some(xdg_surface) = this.try_get(client) {
                                let root_surface_ref = xdg_surface.root_surface_ref;
                                for event in &events {
                                    if let Some(TouchPointerSample {
                                        interaction: Some(interaction),
                                        phase: Some(phase),
                                        position_in_viewport: Some(position_in_viewport),
                                        ..
                                    }) = event.pointer_sample.as_ref()
                                    {
                                        let x = position_in_viewport[0];
                                        let y = position_in_viewport[1];
                                        let xdg_surface_ref =
                                            XdgSurface::get_event_target(root_surface_ref, client)
                                                .unwrap_or(this);
                                        let target =
                                            XdgSurface::hit_test(xdg_surface_ref, x, y, client);
                                        if let Some((_, surface_ref, offset)) = target {
                                            if let Some(surface) = surface_ref.try_get(client) {
                                                let position = {
                                                    let geometry = surface.window_geometry();
                                                    [
                                                        x + geometry.x as f32 - offset.0 as f32,
                                                        y + geometry.y as f32 - offset.1 as f32,
                                                    ]
                                                };

                                                let timestamp = event.timestamp.expect("timestamp");
                                                client
                                                    .input_dispatcher
                                                    .handle_touch_event(
                                                        surface_ref,
                                                        timestamp,
                                                        interaction
                                                            .interaction_id
                                                            .try_into()
                                                            .unwrap(),
                                                        &position,
                                                        *phase,
                                                    )
                                                    .expect("handle_touch_event");
                                            }
                                        }
                                    }
                                }
                            }
                            Ok(())
                        });
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
                        return;
                    }
                    Err(fidl_error) => {
                        println!("touch source Watch() error: {:?}", fidl_error);
                        return;
                    }
                }
            }
        })
        .detach();
    }

    fn spawn_mouse_listener(
        this: ObjectRef<Self>,
        mouse_source: MouseSourceProxy,
        task_queue: TaskQueue,
    ) {
        fasync::Task::local(async move {
            loop {
                let result = mouse_source.watch().await;
                match result {
                    Ok(returned_events) => {
                        let events = returned_events.clone();
                        task_queue.post(move |client| {
                            if let Some(xdg_surface) = this.try_get(client) {
                                let root_surface_ref = xdg_surface.root_surface_ref;
                                for event in &events {
                                    if let Some(MousePointerSample {
                                        device_id: _,
                                        position_in_viewport: Some(position_in_viewport),
                                        relative_motion,
                                        scroll_v,
                                        scroll_h,
                                        pressed_buttons,
                                        ..
                                    }) = event.pointer_sample.as_ref()
                                    {
                                        let x = position_in_viewport[0];
                                        let y = position_in_viewport[1];
                                        let xdg_surface_ref =
                                            XdgSurface::get_event_target(root_surface_ref, client)
                                                .unwrap_or(this);
                                        let target =
                                            XdgSurface::hit_test(xdg_surface_ref, x, y, client);
                                        if let Some((_, surface_ref, offset)) = target {
                                            if let Some(surface) = surface_ref.try_get(client) {
                                                let position = {
                                                    let geometry = surface.window_geometry();
                                                    [
                                                        x + geometry.x as f32 - offset.0 as f32,
                                                        y + geometry.y as f32 - offset.1 as f32,
                                                    ]
                                                };

                                                let timestamp = event.timestamp.expect("timestamp");
                                                client
                                                    .input_dispatcher
                                                    .handle_pointer_event(
                                                        surface_ref,
                                                        timestamp,
                                                        &position,
                                                        pressed_buttons,
                                                        relative_motion,
                                                        scroll_v,
                                                        scroll_h,
                                                    )
                                                    .expect("handle_mouse_event");
                                            }
                                        }
                                    }
                                }
                            }
                            Ok(())
                        });
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
                        return;
                    }
                    Err(fidl_error) => {
                        println!("mouse source Watch() error: {:?}", fidl_error);
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
                            if let Some(xdg_surface) = xdg_surface_ref.try_get(client) {
                                if let Some(view) = xdg_surface.view.as_ref() {
                                    view.lock().handle_view_disconnected(id);
                                }
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
                            if let Some(xdg_surface) = this.try_get(client) {
                                let root_surface_ref = xdg_surface.root_surface_ref;
                                let xdg_surface_ref =
                                    XdgSurface::get_event_target(root_surface_ref, client)
                                        .unwrap_or(this);
                                let surface_ref = xdg_surface_ref.get(client)?.surface_ref;
                                if surface_ref.try_get(client).is_some() {
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
                            }
                            Ok(())
                        });
                    }
                    Err(fidl::Error::ClientChannelClosed { .. }) => {
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

    fn hit_test(
        this: ObjectRef<Self>,
        location_x: f32,
        location_y: f32,
        client: &Client,
    ) -> Option<(ObjectRef<Self>, ObjectRef<Surface>, (i32, i32))> {
        let mut maybe_xdg_surface_ref = Some(this);
        while let Some(xdg_surface_ref) = maybe_xdg_surface_ref.take() {
            if let Some(xdg_surface) = xdg_surface_ref.try_get(client) {
                if let Some((parent_view, view_offset)) = xdg_surface.view.as_ref().map(|v| {
                    let view = v.lock();
                    (view.parent(), view.absolute_offset())
                }) {
                    let surface_ref = xdg_surface.surface_ref;
                    if let Some(surface) = surface_ref.try_get(client) {
                        let x = location_x - view_offset.0 as f32;
                        let y = location_y - view_offset.1 as f32;
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
}

impl RequestReceiver<xdg_shell::XdgSurface> for XdgSurface {
    fn receive(
        this: ObjectRef<Self>,
        request: XdgSurfaceRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            XdgSurfaceRequest::Destroy => {
                client.delete_id(this.id())?;
            }
            XdgSurfaceRequest::GetToplevel { id } => {
                let proxy =
                    connect_to_protocol::<FlatlandMarker>().expect("error connecting to Flatland");
                let flatland = Flatland::new(proxy);
                let toplevel = XdgToplevel::new(this, client, flatland.clone())?;
                let toplevel_ref = id.implement(client, toplevel)?;
                this.get_mut(client)?.set_xdg_role(XdgSurfaceRole::Toplevel(toplevel_ref))?;
                XdgSurface::spawn_flatland_listener(
                    this,
                    client,
                    flatland.borrow().proxy().take_event_stream(),
                )?;
            }
            XdgSurfaceRequest::GetPopup { id, parent, positioner } => {
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
                    flatland.borrow().proxy().take_event_stream(),
                )?;
            }
            XdgSurfaceRequest::SetWindowGeometry { x, y, width, height } => {
                let surface_ref = this.get(client)?.surface_ref;
                surface_ref.get_mut(client)?.enqueue(SurfaceCommand::SetWindowGeometry(Rect {
                    x,
                    y,
                    width,
                    height,
                }));
            }
            XdgSurfaceRequest::AckConfigure { .. } => {}
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
}

impl XdgPopup {
    /// Performs a configure sequence for the XdgPopup object referenced by
    /// `this`.
    pub fn configure(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgPopup::configure");
        let geometry = this.get(client)?.geometry;
        client.event_queue().post(
            this.id(),
            XdgPopupEvent::Configure {
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
        client.event_queue().post(this.id(), XdgPopupEvent::PopupDone)
    }

    /// Creates a new `XdgPopup` surface.
    pub fn new(
        xdg_surface_ref: ObjectRef<XdgSurface>,
        client: &mut Client,
        flatland: FlatlandPtr,
        positioner_ref: ObjectRef<XdgPositioner>,
    ) -> Result<Self, Error> {
        let geometry = positioner_ref.get(client)?.get_geometry()?;
        let surface_ref = xdg_surface_ref.get(client)?.surface_ref();
        surface_ref.get_mut(client)?.set_flatland(flatland)?;
        Ok(XdgPopup { surface_ref, xdg_surface_ref, positioner_ref, geometry })
    }

    pub fn shutdown(&self) {}
}

impl RequestReceiver<xdg_shell::XdgPopup> for XdgPopup {
    fn receive(
        this: ObjectRef<Self>,
        request: XdgPopupRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            XdgPopupRequest::Destroy => {
                let (surface_ref, xdg_surface_ref) = {
                    let popup = this.get(client)?;
                    (popup.surface_ref, popup.xdg_surface_ref)
                };
                xdg_surface_ref.get(client)?.shutdown(client);
                // We need to present here to commit the removal of our
                // popup. This will inform our parent that our view has
                // been destroyed.
                Surface::present_internal(surface_ref, client)?;

                surface_ref.get_mut(client)?.clear_flatland();
                client.delete_id(this.id())?;
            }
            XdgPopupRequest::Grab { .. } => {}
            XdgPopupRequest::Reposition { .. } => {}
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
    /// Maximum size for `XdgToplevel`. A value of zero means no maximum
    /// size in the given dimension.
    max_size: Size,
}

impl XdgToplevel {
    /// Performs a configure sequence for the XdgToplevel object referenced by
    /// `this`.
    pub fn configure(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgToplevel::configure");
        let (width, height, maximized, surface_ref) = {
            let (view, max_size, surface_ref, maybe_parent_ref) = {
                let toplevel = this.get(client)?;
                let max_size = toplevel.max_size;
                let xdg_surface_ref = toplevel.xdg_surface_ref;
                let xdg_surface = xdg_surface_ref.get(client)?;
                (xdg_surface.view.clone(), max_size, toplevel.surface_ref, toplevel.parent_ref)
            };
            // Let the client determine the size if it has a parent.
            let (width, height, maximized) = if maybe_parent_ref.is_some() {
                surface_ref
                    .try_get(client)
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
                (
                    if max_size.width > 0 {
                        physical_size.width.min(max_size.width)
                    } else {
                        physical_size.width
                    },
                    if max_size.height > 0 {
                        physical_size.height.min(max_size.height)
                    } else {
                        physical_size.height
                    },
                    true,
                )
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
            states.push(xdg_toplevel::State::Maximized)?;
        }
        if client.input_dispatcher.has_focus(surface_ref) {
            // If the window has focus, we set the activated state. This is
            // just a hint to pass along to the client so it can draw itself
            // differently with and without focus.
            states.push(xdg_toplevel::State::Activated)?;
        }
        client
            .event_queue()
            .post(this.id(), XdgToplevelEvent::Configure { width, height, states })?;

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

    /// Sets the maximum size for this `XdgToplevel`.
    fn set_max_size(&mut self, max_size: Size) {
        self.max_size = max_size;
    }

    fn close(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "XdgToplevel::close");
        client.event_queue().post(this.id(), XdgToplevelEvent::Close)
    }

    /// Creates a new `XdgToplevel` surface.
    pub fn new(
        xdg_surface_ref: ObjectRef<XdgSurface>,
        client: &mut Client,
        flatland: FlatlandPtr,
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
            max_size: Size { width: 0, height: 0 },
        })
    }

    fn spawn_view_provider(
        this: ObjectRef<Self>,
        client: &mut Client,
        flatland: FlatlandPtr,
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
                            let (touch_source, touch_source_request) =
                                create_proxy::<TouchSourceMarker>()
                                    .expect("failed to create TouchSourceProxy");
                            let (mouse_source, mouse_source_request) =
                                create_proxy::<MouseSourceMarker>()
                                    .expect("failed to create MouseSourceProxy");
                            let view_bound_protocols = ViewBoundProtocols {
                                view_ref_focused: Some(view_ref_focused_request),
                                touch_source: Some(touch_source_request),
                                mouse_source: Some(mouse_source_request),
                                ..ViewBoundProtocols::EMPTY
                            };
                            flatland
                                .borrow()
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
                            XdgSurface::spawn_touch_listener(
                                xdg_surface_ref,
                                touch_source,
                                task_queue.clone(),
                            );
                            XdgSurface::spawn_mouse_listener(
                                xdg_surface_ref,
                                mouse_source,
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
        flatland: FlatlandPtr,
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
        let (touch_source, touch_source_request) =
            create_proxy::<TouchSourceMarker>().expect("failed to create TouchSourceProxy");
        let (mouse_source, mouse_source_request) =
            create_proxy::<MouseSourceMarker>().expect("failed to create MouseSourceProxy");
        let view_bound_protocols = ViewBoundProtocols {
            view_ref_focused: Some(view_ref_focused_request),
            touch_source: Some(touch_source_request),
            mouse_source: Some(mouse_source_request),
            ..ViewBoundProtocols::EMPTY
        };
        flatland
            .borrow()
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
        let max_size = toplevel.max_size;
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
        XdgSurface::spawn_touch_listener(xdg_surface_ref, touch_source, task_queue.clone());
        XdgSurface::spawn_mouse_listener(xdg_surface_ref, mouse_source, task_queue.clone());
        let view_ptr = XdgSurfaceView::new(
            flatland,
            task_queue.clone(),
            xdg_surface_ref,
            surface_ref,
            None,
            Some((0, 0)),
            Rect { x: 0, y: 0, width: max_size.width, height: max_size.height },
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

impl RequestReceiver<xdg_shell::XdgToplevel> for XdgToplevel {
    fn receive(
        this: ObjectRef<Self>,
        request: XdgToplevelRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            XdgToplevelRequest::Destroy => {
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
                Surface::present_internal(surface_ref, client)?;

                surface_ref.get_mut(client)?.clear_flatland();
                client.delete_id(this.id())?;
            }
            XdgToplevelRequest::SetParent { parent } => {
                let toplevel = this.get_mut(client)?;
                let maybe_parent = if parent != 0 { Some(parent.into()) } else { None };
                toplevel.set_parent(maybe_parent);
            }
            XdgToplevelRequest::SetTitle { title } => {
                let toplevel = this.get_mut(client)?;
                toplevel.set_title(Some(title));
            }
            XdgToplevelRequest::SetAppId { .. } => {}
            XdgToplevelRequest::ShowWindowMenu { .. } => {}
            XdgToplevelRequest::Move { .. } => {}
            XdgToplevelRequest::Resize { .. } => {}
            XdgToplevelRequest::SetMaxSize { width, height } => {
                let toplevel = this.get_mut(client)?;
                toplevel.set_max_size(Size { width, height });
                if !toplevel.waiting_for_initial_commit {
                    XdgSurface::configure(toplevel.xdg_surface_ref, client)?;
                }
            }
            XdgToplevelRequest::SetMinSize { .. } => {}
            XdgToplevelRequest::SetMaximized => {}
            XdgToplevelRequest::UnsetMaximized => {}
            XdgToplevelRequest::SetFullscreen { .. } => {}
            XdgToplevelRequest::UnsetFullscreen => {}
            XdgToplevelRequest::SetMinimized => {}
        }
        Ok(())
    }
}

/// A scenic view implementation to back an |XdgSurface| resource.
///
/// An `XdgSurfaceView` will be created by the `ViewProvider` for an
/// `XdgSurface`.
struct XdgSurfaceView {
    flatland: FlatlandPtr,
    root_transform: Option<TransformId>,
    container_transform: TransformId,
    logical_size: SizeF,
    local_offset: Option<(i32, i32)>,
    absolute_offset: (i32, i32),
    task_queue: TaskQueue,
    xdg_surface: ObjectRef<XdgSurface>,
    surface: ObjectRef<Surface>,
    geometry: Rect,
    parent: Option<XdgSurfaceViewPtr>,
    children: BTreeSet<u64>,
}

type XdgSurfaceViewPtr = Arc<Mutex<XdgSurfaceView>>;

impl XdgSurfaceView {
    fn present_internal(&mut self) {
        let surface_ref = self.surface;
        self.task_queue.post(move |client| Surface::present_internal(surface_ref, client));
    }

    fn update_and_present(&mut self) {
        self.update();
        self.present_internal();
    }

    fn reconfigure(&self) {
        // If we have both a size and a pixel scale, we're ready to send the
        // configure event to the client. We need both because we send expose
        // physical pixels to the client.
        if self.logical_size.width != 0.0 && self.logical_size.height != 0.0 {
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
        // Use local offset if we have a parent view.
        parent.as_ref().map_or_else(
            ||
            // Center in available space if geometry is non-zero.
            (
                if geometry.width != 0 {
                    (physical_size.width as i32 - geometry.width) / 2
                } else {
                    0
                },
                if geometry.height != 0 {
                    (physical_size.height as i32 - geometry.height) / 2
                } else {
                    0
                }
            ),
            |parent| {
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
            },
        )
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

    fn parent(&self) -> Option<XdgSurfaceViewPtr> {
        self.parent.clone()
    }

    fn xdg_surface(&self) -> ObjectRef<XdgSurface> {
        self.xdg_surface
    }
}

impl XdgSurfaceView {
    pub fn new(
        flatland: FlatlandPtr,
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
        let root_transform = flatland.borrow_mut().alloc_transform_id();
        let container_transform = flatland.borrow_mut().alloc_transform_id();
        flatland
            .borrow()
            .proxy()
            .create_transform(&mut root_transform.clone())
            .expect("fidl error");
        flatland
            .borrow()
            .proxy()
            .create_transform(&mut container_transform.clone())
            .expect("fidl error");
        let view_controller = XdgSurfaceView {
            flatland,
            root_transform: Some(root_transform),
            container_transform,
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
        if vc.logical_size.width != 0.0 && vc.logical_size.height != 0.0 {
            vc.update();
            vc.reconfigure();
        }
        vc.present_internal();
        Ok(())
    }

    pub fn shutdown(&mut self) {
        self.root_transform.take().map(|_| {
            self.flatland.borrow().proxy().release_view().expect("fidl error");
        });
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
        self.flatland
            .borrow()
            .proxy()
            .add_child(&mut self.container_transform.clone(), &mut surface_transform.clone())
            .expect("fidl error");
        Ok(())
    }

    fn setup_scene(&self) {
        ftrace::duration!("wayland", "XdgSurfaceView::setup_scene");
        self.root_transform.as_ref().map(|root_transform| {
            self.flatland
                .borrow()
                .proxy()
                .set_root_transform(&mut root_transform.clone())
                .expect("fidl error");
            // TODO(fxbug.dev/90666): Add background color if there's no parent.
            self.flatland
                .borrow()
                .proxy()
                .add_child(&mut root_transform.clone(), &mut self.container_transform.clone())
                .expect("fidl error");
        });
    }

    fn update(&mut self) {
        ftrace::duration!("wayland", "XdgSurfaceView::update");
        let mut translation = Vec_ { x: self.absolute_offset.0, y: self.absolute_offset.1 };
        self.flatland
            .borrow()
            .proxy()
            .set_translation(&mut self.container_transform.clone(), &mut translation)
            .expect("fidl error");
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
        self.flatland.borrow().proxy().create_transform(&mut child_transform).expect("fidl error");
        self.flatland
            .borrow()
            .proxy()
            .create_viewport(
                &mut link,
                &mut viewport_creation_token,
                viewport_properties,
                server_end,
            )
            .expect("fidl error");
        self.flatland
            .borrow()
            .proxy()
            .set_content(&mut child_transform, &mut link)
            .expect("fidl error");
        self.root_transform.as_ref().map(|root_transform| {
            self.flatland
                .borrow()
                .proxy()
                .add_child(&mut root_transform.clone(), &mut child_transform)
                .expect("fidl error");
        });
        self.children.insert(id);
        self.update_and_present();
    }

    pub fn handle_view_disconnected(&mut self, id: u64) {
        ftrace::duration!("wayland", "XdgSurfaceView::handle_view_disconnected");
        if self.children.remove(&id) {
            self.root_transform.as_ref().map(|root_transform| {
                let mut child_transform = TransformId { value: id.into() };
                self.flatland
                    .borrow()
                    .proxy()
                    .remove_child(&mut root_transform.clone(), &mut child_transform)
                    .expect("fidl error");
                self.flatland
                    .borrow()
                    .proxy()
                    .release_transform(&mut child_transform)
                    .expect("fidl error");
                let mut link = ContentId { value: id.into() };
                let _ = self.flatland.borrow().proxy().release_viewport(&mut link);
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
            .borrow()
            .proxy()
            .set_viewport_properties(&mut link, viewport_properties)
            .expect("fidl error");
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
        positioner.set_anchor(Enum::Recognized(Anchor::BottomLeft));
        positioner.set_gravity(Enum::Recognized(Gravity::BottomRight));
        assert_eq!(Rect { x: 486, y: 28, width: 168, height: 286 }, positioner.get_geometry()?);
        Ok(())
    }
}
