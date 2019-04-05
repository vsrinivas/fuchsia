// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{format_err, Error};
use fuchsia_scenic as scenic;
use fuchsia_wayland_core as wl;
use wayland::{
    WlCompositor, WlCompositorRequest, WlRegion, WlRegionRequest, WlSurface, WlSurfaceRequest,
};

use crate::client::Client;
use crate::display::Callback;
use crate::object::{NewObjectExt, ObjectRef, RequestReceiver};
use crate::shm::Buffer;

/// An implementation of the wl_compositor global.
pub struct Compositor {
    session: scenic::SessionPtr,
}

impl Compositor {
    /// Creates a new `Compositor`.
    pub fn new(session: scenic::SessionPtr) -> Self {
        Compositor { session }
    }
}

impl RequestReceiver<WlCompositor> for Compositor {
    fn receive(
        this: ObjectRef<Self>,
        request: WlCompositorRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlCompositorRequest::CreateSurface { id } => {
                id.implement(client, Surface::new(this.get(client)?.session.clone()))?;
            }
            WlCompositorRequest::CreateRegion { id } => {
                id.implement(client, Region)?;
            }
        }
        Ok(())
    }
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
    /// The `BufferAttachment` will be set once the client has sent a
    /// wl_surface::attach request and holds a reference to the buffer object
    /// attached to this surface.
    buffer: Option<BufferAttachment>,

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

    frame: Option<ObjectRef<Callback>>,

    /// The scenic node that represents this surface. Views can present this
    /// surface by placeing this node in their view hierarchy.
    node: scenic::ShapeNode,

    /// The scenic session that can be used to create scenic entities.
    scenic: scenic::SessionPtr,
}

impl Surface {
    /// Creates a new `Surface`.
    pub fn new(session: scenic::SessionPtr) -> Self {
        Surface {
            buffer: None,
            role: None,
            scenic: session.clone(),
            node: scenic::ShapeNode::new(session),
            frame: None,
        }
    }

    /// Returns a reference to the `scenic::ShapeNode` for this surface.
    #[allow(dead_code)]
    pub fn node(this: ObjectRef<Self>, client: &mut Client) -> Result<&scenic::ShapeNode, Error> {
        Ok(&this.get(client)?.node)
    }

    /// Returns the `BufferAttachment` for this surface.
    #[allow(dead_code)]
    pub fn buffer(
        this: ObjectRef<Self>,
        client: &mut Client,
    ) -> Result<Option<BufferAttachment>, Error> {
        Ok(this.get(client)?.buffer.clone())
    }

    /// Sets the buffer for this surface.
    ///
    /// This is called internally by a wl_surface::attach request from the
    /// client.
    fn set_buffer(
        this: ObjectRef<Self>,
        client: &mut Client,
        buffer: BufferAttachment,
    ) -> Result<(), Error> {
        this.get_mut(client)?.buffer = Some(buffer);
        Ok(())
    }

    /// Assigns a role to this surface.
    ///
    /// Once a role has been assigned to a surface, it is an error to set a
    /// different role for that same surface.
    #[allow(dead_code)]
    pub fn set_role(
        this: ObjectRef<Self>,
        client: &mut Client,
        role: SurfaceRole,
    ) -> Result<(), Error> {
        let this = this.get_mut(client)?;
        if let Some(current_role) = this.role {
            Err(format_err!(
                "Attemping to reassign surface role from {:?} to {:?}",
                current_role,
                role
            ))
        } else {
            this.role = Some(role);
            Ok(())
        }
    }

    /// Performs the logic to commit the local state of this surface.
    ///
    /// This will update the scenic Node for this surface.
    fn commit_self(&self) -> Result<(), Error> {
        // Update our scenic node with the backing buffer.
        if let Some(buffer_attachment) = self.buffer.clone() {
            let (width, height, image) = {
                let buffer = buffer_attachment.buffer;
                let image_info = buffer.image_info();
                (image_info.width as f32, image_info.height as f32, buffer.create_image())
            };
            self.node.set_shape(&scenic::Rectangle::new(self.scenic.clone(), width, height));
            // The node is placed around it's center, which places most
            // of the surface outside of the view bounds. This places
            // the surface in the top-left corner of the parent node.
            self.node.set_translation(width * 0.5, height * 0.5, 0.0);

            let material = scenic::Material::new(self.scenic.clone());
            material.set_texture(Some(&image));
            self.node.set_material(&material);
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
                client.delete_id(this.id())?;
            }
            WlSurfaceRequest::Attach { buffer, .. } => {
                let buffer = BufferAttachment {
                    buffer_id: buffer,
                    buffer: client.get_object::<Buffer>(buffer)?.clone(),
                };
                Self::set_buffer(this, client, buffer)?;
            }
            WlSurfaceRequest::Frame { callback } => {
                if this.get(client)?.frame.is_some() {
                    return Err(format_err!("Multiple frame requests posted between commits"));
                }
                this.get_mut(client)?.frame = Some(callback.implement(client, Callback)?);
            }
            WlSurfaceRequest::Commit => {
                let (role, frame) = {
                    let surface = this.get_mut(client)?;
                    surface.commit_self()?;
                    (surface.role, surface.frame.take())
                };
                // Notify the role objects that there's been a commit.
                role.map(|role| role.commit(client, frame));
            }
            WlSurfaceRequest::Damage { .. } => {}
            WlSurfaceRequest::SetOpaqueRegion { .. } => {}
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
pub enum SurfaceRole {}

impl SurfaceRole {
    /// Dispatches a commit command to the concrete role objects.
    fn commit(
        &self,
        _client: &mut Client,
        _frame: Option<ObjectRef<Callback>>,
    ) -> Result<(), Error> {
        match *self {}
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
    #[allow(dead_code)]
    pub fn id(&self) -> wl::ObjectId {
        self.buffer_id
    }
}

struct Region;

impl RequestReceiver<WlRegion> for Region {
    fn receive(
        _this: ObjectRef<Self>,
        request: WlRegionRequest,
        _client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlRegionRequest::Destroy => {}
            WlRegionRequest::Add { .. } => {}
            WlRegionRequest::Subtract { .. } => {}
        }
        Ok(())
    }
}
