// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::compositor::{Surface, SurfaceCommand, ViewportCropParams, ViewportScaleParams},
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    anyhow::Error,
    wp_viewporter::{WpViewport, WpViewportRequest, WpViewporter, WpViewporterRequest},
};

/// An implementation of the wp_viewporter global.
pub struct Viewporter;

impl Viewporter {
    /// Creates a new `Viewporter`.
    pub fn new() -> Self {
        Viewporter
    }
}

impl RequestReceiver<WpViewporter> for Viewporter {
    fn receive(
        this: ObjectRef<Self>,
        request: WpViewporterRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WpViewporterRequest::Destroy => {
                client.delete_id(this.id())?;
            }
            WpViewporterRequest::GetViewport { id, surface } => {
                let surface: ObjectRef<Surface> = surface.into();
                id.implement(client, Viewport::new(surface))?;
            }
        }
        Ok(())
    }
}

struct Viewport {
    surface_ref: ObjectRef<Surface>,
}

impl Viewport {
    pub fn new(surface_ref: ObjectRef<Surface>) -> Self {
        Viewport { surface_ref }
    }
}

impl RequestReceiver<WpViewport> for Viewport {
    fn receive(
        this: ObjectRef<Self>,
        request: WpViewportRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WpViewportRequest::Destroy => {
                let surface_ref = this.get(client)?.surface_ref;
                if let Ok(surface) = surface_ref.get_mut(client) {
                    surface.enqueue(SurfaceCommand::ClearViewportCropParams);
                    surface.enqueue(SurfaceCommand::ClearViewportScaleParams);
                }
                client.delete_id(this.id())?;
            }
            WpViewportRequest::SetSource { x, y, width, height } => {
                let (x, y, width, height) =
                    (x.to_float(), y.to_float(), width.to_float(), height.to_float());
                let surface_ref = this.get(client)?.surface_ref;
                let surface = surface_ref.get_mut(client)?;
                if x == -1.0 && y == -1.0 && width == -1.0 && height == -1.0 {
                    surface.enqueue(SurfaceCommand::ClearViewportCropParams);
                } else {
                    surface.enqueue(SurfaceCommand::SetViewportCropParams(ViewportCropParams {
                        x,
                        y,
                        width,
                        height,
                    }));
                }
            }
            WpViewportRequest::SetDestination { width, height } => {
                let surface_ref = this.get(client)?.surface_ref;
                let surface = surface_ref.get_mut(client)?;
                if width == -1 && height == -1 {
                    surface.enqueue(SurfaceCommand::ClearViewportScaleParams);
                } else {
                    surface.enqueue(SurfaceCommand::SetViewportScaleParams(ViewportScaleParams {
                        width,
                        height,
                    }));
                }
            }
        }
        Ok(())
    }
}
