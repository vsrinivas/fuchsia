// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::compositor::{
        PlaceSubsurfaceParams, Surface, SurfaceCommand, SurfaceRelation, SurfaceRole,
    },
    crate::display::Callback,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    anyhow::Error,
    fuchsia_wayland_core as wl,
    std::mem,
    wayland::{WlSubcompositor, WlSubcompositorRequest, WlSubsurface, WlSubsurfaceRequest},
};

#[cfg(not(feature = "flatland"))]
use anyhow::format_err;

/// An implementation of the wl_subcompositor global.
///
/// wl_subcompositor provides an interface for clients to defer some composition
/// to the server. For example, a media player application may provide the
/// compositor with one surface that contains the video frames and another
/// surface that contains playback controls. Deferring this composition to the
/// server allows for the server to make certain optimizations, such as mapping
/// these surfaces to hardware layers on the display controller, if available.
///
/// Implementation Note: We currently implement the wl_subcompositor by creating
/// new scenic ShapeNodes and placing them as children of the parent node. This
/// makes for a simple implementation, but we don't support any alpha blending
/// of subsurfaces (due to limitations in how Scenic handles these use cases).
/// Scenic may be extended to handle these 2D composition use-cases, but without
/// that we'll need to do some of our own blending here.
pub struct Subcompositor;

impl Subcompositor {
    /// Creates a new `Subcompositor`.
    pub fn new() -> Self {
        Subcompositor
    }
}

impl RequestReceiver<WlSubcompositor> for Subcompositor {
    fn receive(
        this: ObjectRef<Self>,
        request: WlSubcompositorRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlSubcompositorRequest::Destroy => {
                client.delete_id(this.id())?;
            }
            WlSubcompositorRequest::GetSubsurface { id, surface, parent } => {
                let subsurface = Subsurface::new(surface, parent);
                let surface_ref = subsurface.surface_ref;
                let parent_ref = subsurface.parent_ref;
                subsurface.attach_to_parent(client)?;
                let subsurface_ref = id.implement(client, subsurface)?;
                surface_ref.get_mut(client)?.set_role(SurfaceRole::Subsurface(subsurface_ref))?;
                parent_ref
                    .get_mut(client)?
                    .enqueue(SurfaceCommand::AddSubsurface(surface_ref, subsurface_ref));
            }
        }
        Ok(())
    }
}

/// Wayland subsurfaces may be in one of two modes, Sync (the default) or Desync.
/// When a wl_subsurface is in sync mode, a wl_surface::commit will simply
/// snapshot the set of pending state. This state will be applied (and changes
/// will appear on screen) when its parent's surface is committed.
///
/// In contrast, a surface in Desync mode will schedule an update to pixels on
/// screen in response to wl_surface::commit.
#[derive(Debug, Copy, Clone, PartialEq)]
pub enum SubsurfaceMode {
    Sync,
    Desync,
}

pub struct Subsurface {
    /// A reference to the backing wl_surface for the subsurface.
    surface_ref: ObjectRef<Surface>,

    /// A reference to the backing wl_surface for the parents surface.
    parent_ref: ObjectRef<Surface>,

    /// The current mode of the surface.
    ///
    /// See `SubsurfaceMode` for more details.
    mode: SubsurfaceMode,

    /// When in sync mode, this is the set of commands that are pending our
    /// parents commit.
    ///
    /// When in desync mode, this must be empty.
    pending_commands: Vec<SurfaceCommand>,

    /// When in sync mode, this is the set of wl_surface::frame callbacks that
    /// are pending our parents commit.
    ///
    /// When in desync mode, this must be empty.
    pending_callbacks: Vec<ObjectRef<Callback>>,
}

impl Subsurface {
    pub fn new(surface: wl::ObjectId, parent: wl::ObjectId) -> Self {
        Self {
            surface_ref: surface.into(),
            parent_ref: parent.into(),
            mode: SubsurfaceMode::Sync,
            pending_commands: Vec::new(),
            pending_callbacks: Vec::new(),
        }
    }

    /// Gets the associated wl_surface for this subsurface.
    pub fn surface(&self) -> ObjectRef<Surface> {
        self.surface_ref
    }

    /// Returns true iff this subsurface is running in synchronized mode.
    pub fn is_sync(&self) -> bool {
        self.mode == SubsurfaceMode::Sync
    }

    /// Adds some `SurfaceCommand`s to this surfaces pending state.
    ///
    /// This `pending state` is a set of operations that were committed with a
    /// wl_surface::commit request, but are waiting for our parents state to
    /// be committed.
    ///
    /// Subsurfaces in desync mode have no pending state, as their state is
    /// applied immediately upon wl_surface::commit.
    pub fn add_pending_commands(&mut self, mut commands: Vec<SurfaceCommand>) {
        assert!(self.is_sync(), "Desync subsurfaces have no pending state");
        self.pending_commands.append(&mut commands);
    }

    /// Extracts the set of pending `SurfaceCommand`s and frame Callbacks for
    /// this subsurface, resetting both to empty vectors.
    pub fn take_pending_state(&mut self) -> (Vec<SurfaceCommand>, Vec<ObjectRef<Callback>>) {
        let commands = mem::replace(&mut self.pending_commands, Vec::new());
        let callbacks = mem::replace(&mut self.pending_callbacks, Vec::new());
        (commands, callbacks)
    }

    pub fn finalize_commit(&mut self, callbacks: &mut Vec<ObjectRef<Callback>>) -> bool {
        if self.is_sync() {
            // If we're in sync mode, we just defer the callbacks until our
            // parents state is applied.
            self.pending_callbacks.append(callbacks);
            false
        } else {
            true
        }
    }
}

#[cfg(feature = "flatland")]
impl Subsurface {
    fn attach_to_parent(&self, _client: &mut Client) -> Result<(), Error> {
        // TODO(fxb/83658): Implement Flatland subsurface support.
        Ok(())
    }

    fn detach_from_parent(&self, _client: &Client) -> Result<(), Error> {
        // TODO(fxb/83658): Implement Flatland subsurface support.
        Ok(())
    }
}

#[cfg(not(feature = "flatland"))]
impl Subsurface {
    fn attach_to_parent(&self, client: &mut Client) -> Result<(), Error> {
        let session = match self.parent_ref.get(client)?.session() {
            Some(s) => s.clone(),
            None => return Err(format_err!("Parent surface has no session!")),
        };
        self.surface_ref.get_mut(client)?.set_session(session)?;

        let pixel_scale = self.parent_ref.get(client)?.pixel_scale();
        self.parent_ref.get_mut(client)?.set_pixel_scale(pixel_scale.0, pixel_scale.1);

        // Unwrap here since we have just determined both surfaces have a
        // session, which is the only prerequisite for having a node.
        let parent_node = self.parent_ref.get(client)?.node().unwrap();
        let child_node = self.surface_ref.get(client)?.node().unwrap();
        parent_node.add_child(child_node);

        Ok(())
    }

    fn detach_from_parent(&self, client: &Client) -> Result<(), Error> {
        if let Some(child_node) = self.surface_ref.get(client)?.node() {
            child_node.detach();
        }
        Ok(())
    }
}

impl RequestReceiver<WlSubsurface> for Subsurface {
    fn receive(
        this: ObjectRef<Self>,
        request: WlSubsurfaceRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlSubsurfaceRequest::Destroy => {
                let parent_ref = {
                    let subsurface = this.get(client)?;
                    subsurface.detach_from_parent(client)?;
                    subsurface.parent_ref
                };
                parent_ref.get_mut(client)?.detach_subsurface(this);
                client.delete_id(this.id())?;
            }
            WlSubsurfaceRequest::SetPosition { x, y } => {
                let surface_ref = this.get(client)?.surface_ref;
                surface_ref.get_mut(client)?.enqueue(SurfaceCommand::SetPosition(x, y));
            }
            WlSubsurfaceRequest::PlaceAbove { sibling } => {
                let parent_ref = this.get(client)?.parent_ref;
                parent_ref.get_mut(client)?.enqueue(SurfaceCommand::PlaceSubsurface(
                    PlaceSubsurfaceParams {
                        subsurface: this,
                        sibling: sibling.into(),
                        relation: SurfaceRelation::Above,
                    },
                ));
            }
            WlSubsurfaceRequest::PlaceBelow { sibling } => {
                let parent_ref = this.get(client)?.parent_ref;
                parent_ref.get_mut(client)?.enqueue(SurfaceCommand::PlaceSubsurface(
                    PlaceSubsurfaceParams {
                        subsurface: this,
                        sibling: sibling.into(),
                        relation: SurfaceRelation::Below,
                    },
                ));
            }
            // Note that SetSync and SetDesync are not double buffered as is
            // most state:
            //
            //   This state is applied when the parent surface's wl_surface
            //   state is applied, regardless of the sub-surface's mode. As the
            //   exception, set_sync and set_desync are effective immediately.
            WlSubsurfaceRequest::SetSync => {
                this.get_mut(client)?.mode = SubsurfaceMode::Sync;
            }
            WlSubsurfaceRequest::SetDesync => {
                let (commands, callbacks, surface_ref) = {
                    let this = this.get_mut(client)?;
                    let (commands, callbacks) = this.take_pending_state();
                    (commands, callbacks, this.surface_ref)
                };
                // TODO(tjdetwiler): We should instead schedule these callbacks
                // on the wl_surface immediately. This needs a small refactor of
                // wl_surface to make this possible.
                assert!(callbacks.is_empty());
                let surface = surface_ref.get_mut(client)?;
                for command in commands {
                    surface.enqueue(command);
                }
                this.get_mut(client)?.mode = SubsurfaceMode::Desync;
            }
        }
        Ok(())
    }
}
