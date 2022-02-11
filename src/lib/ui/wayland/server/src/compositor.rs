// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::buffer::Buffer,
    crate::buffer::ImageInstanceId,
    crate::client::{Client, TaskQueue},
    crate::display::Callback,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    crate::scenic::FlatlandPtr,
    crate::subcompositor::Subsurface,
    crate::xdg_shell::XdgSurface,
    anyhow::{format_err, Error},
    fidl_fuchsia_math::{Rect, RectF, Size, SizeU, Vec_},
    fidl_fuchsia_ui_composition::{BlendMode, TransformId},
    fuchsia_async as fasync, fuchsia_trace as ftrace, fuchsia_wayland_core as wl,
    fuchsia_zircon::{self as zx, HandleBased},
    std::mem,
    std::sync::atomic::{AtomicUsize, Ordering},
    wayland_server_protocol::{
        WlBufferEvent, WlCompositor, WlCompositorRequest, WlRegion, WlRegionRequest, WlSurface,
        WlSurfaceRequest,
    },
};

static NEXT_IMAGE_INSTANCE_ID: AtomicUsize = AtomicUsize::new(1);

/// An implementation of the wl_compositor global.
pub struct Compositor;

impl Compositor {
    /// Creates a new `Compositor`.
    pub fn new() -> Self {
        Self
    }
}

impl RequestReceiver<WlCompositor> for Compositor {
    fn receive(
        _this: ObjectRef<Self>,
        request: WlCompositorRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlCompositorRequest::CreateSurface { id } => {
                let surface_id = id.id();
                id.implement(client, Surface::new(surface_id))?;
            }
            WlCompositorRequest::CreateRegion { id } => {
                id.implement(client, Region { rects: vec![] })?;
            }
        }
        Ok(())
    }
}

/// A `SurfaceNode` manages the set of flatland resources associated with a
/// surface.
struct SurfaceNode {
    /// The flatland instance that can be used to create flatland entities.
    pub flatland: FlatlandPtr,
    /// The flatland transform that represents this surface. Views can present this
    /// surface by placeing this transform in their view hierarchy.
    pub transform_id: TransformId,
}

impl SurfaceNode {
    pub fn new(flatland: FlatlandPtr) -> Self {
        let transform_id = flatland.borrow_mut().alloc_transform_id();
        flatland.borrow().proxy().create_transform(&mut transform_id.clone()).expect("fidl error");
        Self { flatland, transform_id }
    }
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub struct ViewportCropParams {
    pub x: f32,
    pub y: f32,
    pub width: f32,
    pub height: f32,
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub struct ViewportScaleParams {
    pub width: i32,
    pub height: i32,
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub enum SurfaceRelation {
    Above,
    Below,
}

#[derive(Debug, Copy, Clone, PartialEq)]
pub struct PlaceSubsurfaceParams {
    pub subsurface: ObjectRef<Subsurface>,
    pub sibling: ObjectRef<Surface>,
    pub relation: SurfaceRelation,
}

pub enum SurfaceCommand {
    AttachBuffer(BufferAttachment),
    ClearBuffer,
    Frame(ObjectRef<Callback>),
    SetOpaqueRegion(Region),
    SetViewportCropParams(ViewportCropParams),
    ClearViewportCropParams,
    SetViewportScaleParams(ViewportScaleParams),
    ClearViewportScaleParams,
    SetWindowGeometry(Rect),
    SetPosition(i32, i32),
    AddSubsurface(ObjectRef<Surface>, ObjectRef<Subsurface>),
    PlaceSubsurface(PlaceSubsurfaceParams),
}

/// A Surface is the object backing wl_surface protocol objects.
///
/// A Surface alone is not of much use until it's been assigned a role. Surface
/// roles are assigned implicitly when binding the role object to the surface.
///
/// For example, the request wl_shell::get_shell_surface(new_id, object<wl_surface>)
/// will create a new wl_shell_surface object for the provided surface. When
/// this happens we assign the `wl_shell_surface` role to the underlying
/// `wl_surface` object. Once a surface has been assigned a role, it is an error
/// to attempt to assign it a different role.
pub struct Surface {
    /// The current size of this surface as determined by the currently attached
    /// buffer and scale.
    ///
    /// If no buffer is currently associated with this surface the size will be
    /// (0, 0).
    size: Size,

    /// The position of this surface with its parent.
    ///
    /// If this surface has no parent, its position will be (0, 0).
    position: (i32, i32),

    /// The relative z-ordering of this surface relative to other subsurfaces.
    ///
    /// Surfaces with a higher z-order will be drawn over surfaces with a lower
    /// z-order.
    ///
    /// TODO: This is sufficient to implement a single wl_surface with an
    /// arbitrary number of wl_subsurfaces. We'll need to make this more
    /// intelligent to handle the case of nested subsurfaces (that is, a sub-
    /// surface that has child subsurfaces itself).
    z_order: usize,

    /// The assgned role for this surface. This is set to `None` on creation
    /// and is implicitly set when creating the role object.
    ///
    /// Ex:
    ///
    ///   xdg_shell::get_xdg_surface(new_id<xdg_surface>, object<wl_surface>)
    ///
    /// The above request in the xdg_shell interface creates a new xdg_surface
    /// object for the provided wl_surface. This request would assign the
    /// xdg_surface role to the wl_surface.
    role: Option<SurfaceRole>,

    /// The opaque region for this surface.
    ///
    /// This determines what blend mode to use for surface.
    opaque_region: Region,

    /// The crop parameters are set by a wp_viweport::set_source request.
    ///
    /// If set, this determines how a surface should be cropped to the viewport.
    crop_params: Option<ViewportCropParams>,

    /// The scale parameters are set by a wp_viweport::set_destination request.
    ///
    /// If set, this determines how a surface should be scaled to the viewport.
    scale_params: Option<ViewportScaleParams>,

    /// Callbacks set by the client for redraw hints.
    frame_callbacks: Vec<ObjectRef<Callback>>,

    /// The set of scenic node resources that implement this surface. Initially
    /// this is `None` and becomes populated when a scenic session has been
    /// associated with the surface (see `Surface::set_session`).
    ///
    /// For a surface to be presented, it must have an assigned scenic session
    /// and this node must be included in a scenic resource tree that is mapped
    /// to a View (see XdgToplevel).
    node: Option<SurfaceNode>,

    /// The window geometry defines the portion of the surface that should be
    /// considered the primary window region. Portions of the surface outside
    /// of the window geometry may contain additional detail and/or client-side
    /// decorations.
    ///
    /// If unset then the entire surface bounds will be used as the window
    /// geometry.
    window_geometry: Option<Rect>,

    /// The set of commands that have been queued up, pending the next commit.
    pending_commands: Vec<SurfaceCommand>,

    /// The set of active subsurfaces attached to this surface.
    ///
    /// The first element of the tuple is the wl_surface id for the surface. The
    /// second tuple element is the wl_subsurface id of the subsurface. The
    /// parent will be inserted into this vector with `None` for the subsurface
    /// ref, which enables this vector to track the current subsurface ordering
    /// of all subsurfaces and the parent.
    subsurfaces: Vec<(ObjectRef<Surface>, Option<ObjectRef<Subsurface>>)>,

    /// Parent and offset that can be set using aura shell interface.
    parent: Option<ObjectRef<Surface>>,
    offset: Option<(i32, i32)>,

    /// Frame callbacks for next present.
    present_callbacks: Vec<ObjectRef<Callback>>,

    /// Present credits that determine if we are allowed to present.
    present_credits: u32,

    /// Set after we tried to Present but had no remaining credits. This is used
    /// to trigger a present as soon as we a credit.
    present_needed: bool,

    /// Frame callbacks for OnNextFrameBegin event.
    on_next_frame_begin_callbacks: Vec<ObjectRef<Callback>>,

    /// Global identifier for image instances used by this surface.
    image_instance_id: ImageInstanceId,

    /// The current content of this surface as determined by the currently attached
    /// buffer.
    content: Option<BufferAttachment>,
}

impl Surface {
    /// Enqueues a command for this surface to take effect on the next call to
    /// wl_surface::commit.
    pub fn enqueue(&mut self, command: SurfaceCommand) {
        self.pending_commands.push(command);
    }

    pub fn detach_subsurface(&mut self, subsurface_ref: ObjectRef<Subsurface>) {
        if let Some(index) = self.subsurfaces.iter().position(|x| x.1 == Some(subsurface_ref)) {
            self.subsurfaces.remove(index);
        }
    }

    /// Assigns a role to this surface.
    ///
    /// The role can be updated as long as the type of role remains the same,
    /// it is an error to set a different type of role for that same surface.
    pub fn set_role(&mut self, role: SurfaceRole) -> Result<(), Error> {
        ftrace::duration!("wayland", "Surface::set_role");
        // The role is valid unless a different role has been assigned before.
        let valid_role = match &self.role {
            Some(SurfaceRole::XdgSurface(_)) => match role {
                SurfaceRole::XdgSurface(_) => true,
                _ => false,
            },
            Some(SurfaceRole::Subsurface(_)) => match role {
                SurfaceRole::Subsurface(_) => true,
                _ => false,
            },
            _ => true,
        };
        if valid_role {
            self.role = Some(role);
            Ok(())
        } else {
            Err(format_err!(
                "Attemping to reassign surface role from {:?} to {:?}",
                self.role,
                role
            ))
        }
    }

    pub fn set_parent_and_offset(&mut self, parent: Option<ObjectRef<Surface>>, x: i32, y: i32) {
        self.parent = parent;
        self.offset = Some((x, y));
    }

    pub fn window_geometry(&self) -> Rect {
        if let Some(window_geometry) = self.window_geometry.as_ref() {
            Rect { ..*window_geometry }
        } else {
            Rect { x: 0, y: 0, width: self.size.width, height: self.size.height }
        }
    }

    pub fn offset(&self) -> Option<(i32, i32)> {
        self.offset
    }

    // TODO: Determine correct error handling.
    fn commit_subsurfaces(
        client: &mut Client,
        callbacks: &mut Vec<ObjectRef<Callback>>,
        subsurfaces: &[(ObjectRef<Surface>, Option<ObjectRef<Subsurface>>)],
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "Surface::commit_subsurfaces");
        for (index, entry) in subsurfaces.iter().enumerate() {
            entry.0.get_mut(client)?.z_order = index;
            if let Some(subsurface_ref) = entry.1 {
                if subsurface_ref.get(client)?.is_sync() {
                    // Get pending commands from subsurface
                    let mut pending_state = subsurface_ref.get_mut(client)?.take_pending_state();
                    let task_queue = client.task_queue();
                    let surface_ref = subsurface_ref.get(client)?.surface();
                    let surface = surface_ref.get_mut(client)?;
                    surface.pending_commands.append(&mut pending_state.0);
                    callbacks.append(&mut pending_state.1);
                    surface.commit_self(task_queue, callbacks)?;
                }
            }
        }

        Ok(())
    }

    pub fn hit_test(
        &self,
        x: f32,
        y: f32,
        client: &Client,
    ) -> Option<(ObjectRef<Self>, (i32, i32))> {
        // Iterate over subsurfaces, starting with the top-most surface.
        for (surface_ref, _) in self.subsurfaces.iter().rev() {
            if let Some(surface) = surface_ref.try_get(client) {
                let (x1, y1, x2, y2) = {
                    let geometry = surface.window_geometry();
                    (
                        surface.position.0,
                        surface.position.1,
                        surface.position.0 + geometry.width,
                        surface.position.1 + geometry.height,
                    )
                };
                if x >= x1 as f32 && y >= y1 as f32 && x <= x2 as f32 && y <= y2 as f32 {
                    return Some((*surface_ref, surface.position));
                }
            }
        }

        None
    }

    /// Creates a new `Surface`.
    pub fn new(id: wl::ObjectId) -> Self {
        Surface {
            size: Size { width: 0, height: 0 },
            position: (0, 0),
            z_order: 0,
            role: None,
            opaque_region: Region { rects: vec![] },
            crop_params: None,
            scale_params: None,
            frame_callbacks: vec![],
            node: None,
            window_geometry: None,
            parent: None,
            offset: None,
            pending_commands: Vec::new(),
            subsurfaces: vec![(id.into(), None)],
            present_callbacks: vec![],
            present_credits: 1,
            present_needed: false,
            on_next_frame_begin_callbacks: vec![],
            image_instance_id: NEXT_IMAGE_INSTANCE_ID.fetch_add(1, Ordering::Relaxed),
            content: None,
        }
    }

    /// Assigns the Flatland instance for this surface.
    ///
    /// When a surface is initially created, it has no Flatland instance. Since
    /// the instance is used to create the Flatland resources backing the surface,
    /// a wl_surface _must_ have an assigned an instance before it is committed.
    ///
    /// Ex: for xdg_toplevel surfaces, the a new instance will be created for
    /// each toplevel.
    ///
    /// It is an error to call `set_flatland` multiple times for the same
    /// surface.
    pub fn set_flatland(&mut self, flatland: FlatlandPtr) -> Result<(), Error> {
        ftrace::duration!("wayland", "Surface::set_flatland");
        if self.node.is_some() {
            Err(format_err!("Changing the Flatland instance for a surface is not supported"))
        } else {
            self.node = Some(SurfaceNode::new(flatland));
            Ok(())
        }
    }

    pub fn clear_flatland(&mut self) {
        self.node = None;
    }

    pub fn flatland(&self) -> Option<FlatlandPtr> {
        self.node.as_ref().map(|n| n.flatland.clone())
    }

    /// Returns a reference to the `TransformId` for this surface.
    pub fn transform(&self) -> Option<&TransformId> {
        self.node.as_ref().map(|n| &n.transform_id)
    }

    pub fn take_on_next_frame_begin_callbacks(&mut self) -> Vec<ObjectRef<Callback>> {
        mem::replace(&mut self.on_next_frame_begin_callbacks, vec![])
    }

    /// Updates the current surface state by applying a single `SurfaceCommand`.
    fn apply(&mut self, command: SurfaceCommand) -> Result<(), Error> {
        match command {
            SurfaceCommand::AttachBuffer(attachment) => {
                self.content = Some(attachment.clone());
            }
            SurfaceCommand::ClearBuffer => {}
            SurfaceCommand::Frame(callback) => {
                self.frame_callbacks.push(callback);
            }
            SurfaceCommand::SetOpaqueRegion(region) => {
                self.opaque_region = region;
            }
            SurfaceCommand::SetViewportCropParams(params) => {
                self.crop_params = Some(params);
            }
            SurfaceCommand::ClearViewportCropParams => {
                self.crop_params = None;
            }
            SurfaceCommand::SetViewportScaleParams(params) => {
                self.scale_params = Some(params);
            }
            SurfaceCommand::ClearViewportScaleParams => {
                self.scale_params = None;
            }
            SurfaceCommand::SetWindowGeometry(geometry) => {
                self.window_geometry = Some(geometry);
            }
            SurfaceCommand::SetPosition(x, y) => {
                self.position = (x, y);
            }
            SurfaceCommand::AddSubsurface(surface_ref, subsurface_ref) => {
                self.subsurfaces.push((surface_ref, Some(subsurface_ref)));
            }
            SurfaceCommand::PlaceSubsurface(params) => {
                let sibling_index = if let Some(index) =
                    self.subsurfaces.iter().position(|x| x.0 == params.sibling)
                {
                    index
                } else {
                    return Err(format_err!("Invalid sibling id {}", params.sibling.id()));
                };
                let sibling_entry = self.subsurfaces.remove(sibling_index);
                let anchor_index = if let Some(index) =
                    self.subsurfaces.iter().position(|x| x.1 == Some(params.subsurface))
                {
                    index
                } else {
                    return Err(format_err!("Invalid subsurface id {}", params.subsurface.id()));
                };

                let new_index = match params.relation {
                    SurfaceRelation::Below => anchor_index,
                    SurfaceRelation::Above => anchor_index + 1,
                };
                self.subsurfaces.insert(new_index, sibling_entry);
            }
        };
        Ok(())
    }

    /// Performs the logic to commit the local state of this surface.
    ///
    /// This will update the scenic Node for this surface.
    fn commit_self(
        &mut self,
        task_queue: TaskQueue,
        callbacks: &mut Vec<ObjectRef<Callback>>,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "Surface::commit_self");

        // Save the last buffer ID before applying updates.
        let last_buffer_id = self.content.as_ref().map(|content| content.id());

        let commands = mem::replace(&mut self.pending_commands, Vec::new());
        for command in commands {
            self.apply(command)?;
        }

        let node = match self.node.as_ref() {
            Some(node) => node,
            None => {
                // This is expected for some surfaces that aren't implemented
                // yet, like wl_pointer cursor surfaces.
                println!(
                    "No flatland instance associated with surface role {:?}; skipping commit",
                    self.role
                );
                return Ok(());
            }
        };

        if let Some(content) = &self.content {
            // Acquire image content. The instance ID ensures that usage of buffer by
            // another surface will not conflict with this surface.
            let image_content =
                content.buffer.image_content(self.image_instance_id, &node.flatland);
            let image_size = content.buffer.image_size();

            // Set image as content for transform.
            node.flatland
                .borrow()
                .proxy()
                .set_content(&mut node.transform_id.clone(), &mut image_content.id.clone())
                .expect("fidl error");

            // Set image sample region based on current crop params.
            let mut sample_region = self.crop_params.map_or(
                RectF {
                    x: 0.0,
                    y: 0.0,
                    width: image_size.width as f32,
                    height: image_size.height as f32,
                },
                |crop| RectF { x: crop.x, y: crop.y, width: crop.width, height: crop.height },
            );
            node.flatland
                .borrow()
                .proxy()
                .set_image_sample_region(&mut image_content.id.clone(), &mut sample_region)
                .expect("fidl error");

            // Set destination size based on current scale params.
            let mut destination_size = self.scale_params.map_or(
                SizeU { width: image_size.width as u32, height: image_size.height as u32 },
                |scale| SizeU { width: scale.width as u32, height: scale.height as u32 },
            );
            node.flatland
                .borrow()
                .proxy()
                .set_image_destination_size(&mut image_content.id.clone(), &mut destination_size)
                .expect("fidl error");

            // Update surface size. This is used to determine window geometry and blend mode.
            self.size = Size {
                width: destination_size.width as i32,
                height: destination_size.height as i32,
            };

            // Set blend mode based on the opaque region and if the buffer
            // has an alpha channel.
            let blend_mode = if content.buffer.has_alpha() {
                // Blending is not required if opaque region is set and
                // matches the size of the surface.
                if !self.opaque_region.rects.is_empty()
                    && self.opaque_region.rects.iter().all(|r| {
                        *r == (
                            RectKind::Add,
                            Rect { x: 0, y: 0, width: self.size.width, height: self.size.height },
                        )
                    })
                {
                    BlendMode::Src
                } else {
                    BlendMode::SrcOver
                }
            } else {
                BlendMode::Src
            };
            node.flatland
                .borrow()
                .proxy()
                .set_image_blending_function(&mut image_content.id.clone(), blend_mode)
                .expect("fidl error");
        }

        let mut translation = if let Some(window_geometry) = self.window_geometry.as_ref() {
            Vec_ { x: self.position.0 - window_geometry.x, y: self.position.1 - window_geometry.y }
        } else {
            Vec_ { x: self.position.0, y: self.position.1 }
        };
        node.flatland
            .borrow()
            .proxy()
            .set_translation(&mut node.transform_id.clone(), &mut translation)
            .expect("fidl error");

        // Create and register a release fence to release the last buffer unless
        // it's the same as the current buffer.
        // TODO(fxbug.dev/85402): Track multiple usages of the same buffer and only
        // generate the release event when all usages drop to zero.
        let buffer_id = self.content.as_ref().map(|content| content.id());
        if last_buffer_id != buffer_id {
            if let Some(last_buffer_id) = last_buffer_id {
                let release_fence = zx::Event::create().unwrap();
                node.flatland.borrow_mut().add_release_fence(
                    release_fence.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap(),
                );
                let task_queue = task_queue.clone();
                fasync::Task::local(async move {
                    let _signals =
                        fasync::OnSignals::new(&release_fence, zx::Signals::EVENT_SIGNALED)
                            .await
                            .unwrap();
                    // Safe to ignore result as EVENT_SIGNALED must have
                    // been observed if we reached this.
                    task_queue.post(move |client| {
                        client.event_queue().post(last_buffer_id, WlBufferEvent::Release)
                    });
                })
                .detach();
            }
        }

        callbacks.append(&mut self.frame_callbacks);

        Ok(())
    }

    fn present_now(&mut self) -> Result<(), Error> {
        ftrace::duration!("wayland", "Surface::present_now");
        let flatland = self
            .flatland()
            .ok_or(format_err!("Unable to present surface without a flatland instance."))?;
        // Wayland protocol doesn't provide a mechanism to control presentation time
        // so we ask Flatland to present contents immediately by specifying a presentation
        // time of 0.
        flatland.borrow_mut().present(0);
        self.present_credits -= 1;
        self.present_needed = false;
        let mut callbacks = mem::replace(&mut self.present_callbacks, vec![]);
        self.on_next_frame_begin_callbacks.append(&mut callbacks);
        Ok(())
    }

    pub fn present_internal(this: ObjectRef<Self>, client: &mut Client) -> Result<(), Error> {
        ftrace::duration!("wayland", "Surface::present_internal");
        if let Some(surface) = this.try_get_mut(client) {
            if surface.present_credits == 0 {
                surface.present_needed = true;
            } else {
                surface.present_now()?;
            }
        }
        Ok(())
    }

    pub fn present(
        this: ObjectRef<Self>,
        client: &mut Client,
        mut callbacks: Vec<ObjectRef<Callback>>,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "Surface::present");
        if let Some(surface) = this.try_get_mut(client) {
            surface.present_callbacks.append(&mut callbacks);
            if surface.present_credits == 0 {
                surface.present_needed = true;
            } else {
                surface.present_now()?;
            }
        }
        Ok(())
    }

    pub fn add_present_credits(
        this: ObjectRef<Self>,
        client: &mut Client,
        present_credits: u32,
    ) -> Result<(), Error> {
        ftrace::duration!("wayland", "Surface::add_present_credits");
        if let Some(surface) = this.try_get_mut(client) {
            surface.present_credits += present_credits;
            // Present immediately if needed.
            if surface.present_needed && surface.present_credits > 0 {
                surface.present_now()?;
            }
        }
        Ok(())
    }
}

impl RequestReceiver<WlSurface> for Surface {
    fn receive(
        this: ObjectRef<Self>,
        request: WlSurfaceRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlSurfaceRequest::Destroy => {
                client.input_dispatcher.clear_focus_on_surface_destroy(this);
                client.delete_id(this.id())?;
            }
            WlSurfaceRequest::Attach { buffer, .. } => {
                if buffer == 0 {
                    this.get_mut(client)?.enqueue(SurfaceCommand::ClearBuffer);
                } else {
                    let attachment = BufferAttachment {
                        buffer_id: buffer,
                        buffer: client.get_object::<Buffer>(buffer)?.clone(),
                    };
                    this.get_mut(client)?.enqueue(SurfaceCommand::AttachBuffer(attachment));
                }
            }
            WlSurfaceRequest::Frame { callback } => {
                let callback = callback.implement(client, Callback)?;
                this.get_mut(client)?.enqueue(SurfaceCommand::Frame(callback));
            }
            WlSurfaceRequest::Commit => {
                let mut callbacks = Vec::new();
                let role = {
                    if let Some(SurfaceRole::Subsurface(subsurface_ref)) = this.get(client)?.role {
                        if subsurface_ref.get(client)?.is_sync() {
                            // We're a sync subsurface. We don't want to commit
                            // self yet. Rather we extract the current pending
                            // commands and defer them to be applied when our
                            // parent is committed.
                            let commands = {
                                let surface = this.get_mut(client)?;
                                mem::replace(&mut surface.pending_commands, Vec::new())
                            };
                            subsurface_ref.get_mut(client)?.add_pending_commands(commands);
                            return Ok(());
                        }
                    }

                    // If we're not a sync subsurface, we proceed with committing our
                    // state.
                    let task_queue = client.task_queue();
                    let surface = this.get_mut(client)?;
                    surface.commit_self(task_queue.clone(), &mut callbacks)?;
                    surface.role
                };

                // We're applying our state so we need to apply any state associated
                // with sync subsurfaces.
                {
                    // We briefly extract the subsurface vector from the surface
                    // to allow us to iterate over the subsurfaces to commit.
                    // We need to perform some mutable operations here (changing
                    // z-index), so this is safe as long as no new subsurfaces
                    // are added. That should never happen, and we assert that
                    // the subsurface vector is indeed empty when we re-insert
                    // the subsurface vector back into the surface.
                    let subsurfaces =
                        mem::replace(&mut this.get_mut(client)?.subsurfaces, Vec::new());
                    let result =
                        Self::commit_subsurfaces(client, &mut callbacks, subsurfaces.as_slice());
                    let subsurfaces =
                        mem::replace(&mut this.get_mut(client)?.subsurfaces, subsurfaces);
                    assert!(subsurfaces.is_empty());
                    result?;
                }

                // Notify the role objects that there's been a commit. This hook will
                // return a boolean indicating if the role needs a present. For example,
                // an xdg_toplevel will need a Present to get its newly updated state
                // onto the screen, but a sync wl_subsurface wants to defer a present
                // until its parent state is committed.
                let needs_present = role
                    .map(|role| role.finalize_commit(client, &mut callbacks))
                    .unwrap_or(Ok(false))?;

                // We trigger a present if explicitly requested of if there are any
                // remaining frame callbacks.
                if needs_present || !callbacks.is_empty() {
                    Self::present(this, client, callbacks)?;
                }
            }
            WlSurfaceRequest::Damage { .. } => {}
            WlSurfaceRequest::SetOpaqueRegion { region } => {
                let r = if region == 0 {
                    Region { rects: vec![] }
                } else {
                    client.get_object::<Region>(region)?.clone()
                };
                this.get_mut(client)?.enqueue(SurfaceCommand::SetOpaqueRegion(r));
            }
            WlSurfaceRequest::SetInputRegion { .. } => {}
            WlSurfaceRequest::SetBufferTransform { .. } => {}
            WlSurfaceRequest::SetBufferScale { .. } => {}
            WlSurfaceRequest::DamageBuffer { .. } => {}
        }
        Ok(())
    }
}

/// `SurfaceRole` holds the set of every role that can be assigned to a
/// wl_surface. Each variant will hold an `ObjectRef` to the role object.
#[derive(Copy, Clone, Debug)]
pub enum SurfaceRole {
    /// The surface is an xdg_surface. Note that xdg_surface isn't a role
    /// itself, but instead maps to sub-roles (ex: xdg_toplevel). We'll let
    /// the `XdgSurface` handle the xdg sub-roles, however.
    XdgSurface(ObjectRef<XdgSurface>),
    Subsurface(ObjectRef<Subsurface>),
}

impl SurfaceRole {
    /// Dispatches a commit command to the concrete role objects.
    fn finalize_commit(
        &self,
        client: &mut Client,
        callbacks: &mut Vec<ObjectRef<Callback>>,
    ) -> Result<bool, Error> {
        ftrace::duration!("wayland", "SurfaceRole::commit");
        match self {
            SurfaceRole::XdgSurface(xdg_surface_ref) => {
                XdgSurface::finalize_commit(*xdg_surface_ref, client)
            }
            SurfaceRole::Subsurface(subsurface_ref) => {
                Ok(subsurface_ref.get_mut(client)?.finalize_commit(callbacks))
            }
        }
    }
}

/// A `BufferAttachment` holds the state of the attached buffer to a `Surface`.
///
/// This amount to the set of arguments to the most recently received
/// `wl_surface::attach` request.
#[derive(Clone)]
pub struct BufferAttachment {
    pub buffer_id: wl::ObjectId,
    /// The buffer object.
    pub buffer: Buffer,
    // TODO(tjdetwiler): Add x, y parameters from wl_surface::attach.
}

impl BufferAttachment {
    pub fn id(&self) -> wl::ObjectId {
        self.buffer_id
    }
}

#[derive(PartialEq, Clone, Debug)]
pub enum RectKind {
    Add,
    Subtract,
}

#[derive(PartialEq, Clone, Debug)]
pub struct Region {
    pub rects: Vec<(RectKind, Rect)>,
}

impl RequestReceiver<WlRegion> for Region {
    fn receive(
        this: ObjectRef<Self>,
        request: WlRegionRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlRegionRequest::Destroy => {
                client.delete_id(this.id())?;
            }
            WlRegionRequest::Add { x, y, width, height } => {
                let region = this.get_mut(client)?;
                region.rects.push((RectKind::Add, Rect { x, y, width, height }));
            }
            WlRegionRequest::Subtract { x, y, width, height } => {
                let region = this.get_mut(client)?;
                region.rects.push((RectKind::Subtract, Rect { x, y, width, height }));
            }
        }
        Ok(())
    }
}
