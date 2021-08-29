// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::compositor::Surface,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    anyhow::Error,
    fuchsia_wayland_core as wl,
    zaura_shell::{
        ZauraOutput, ZauraOutputRequest, ZauraShell, ZauraShellRequest, ZauraSurface,
        ZauraSurfaceRequest,
    },
};

/// An implementation of the zaura_shell global.
pub struct AuraShell;

impl AuraShell {
    /// Creates a new `AuraShell`.
    pub fn new() -> Self {
        Self
    }
}

impl RequestReceiver<ZauraShell> for AuraShell {
    fn receive(
        _this: ObjectRef<Self>,
        request: ZauraShellRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZauraShellRequest::GetAuraSurface { id, surface } => {
                id.implement(client, AuraSurface::new(surface))?;
            }
            ZauraShellRequest::GetAuraOutput { id, .. } => {
                id.implement(client, AuraOutput::new())?;
            }
        }
        Ok(())
    }
}

struct AuraSurface {
    surface_ref: ObjectRef<Surface>,
}

impl AuraSurface {
    pub fn new(surface: wl::ObjectId) -> Self {
        Self { surface_ref: surface.into() }
    }
}

impl RequestReceiver<ZauraSurface> for AuraSurface {
    fn receive(
        this: ObjectRef<Self>,
        request: ZauraSurfaceRequest,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZauraSurfaceRequest::SetFrame { .. } => {}
            ZauraSurfaceRequest::SetParent { .. } => {}
            ZauraSurfaceRequest::SetFrameColors { .. } => {}
            ZauraSurfaceRequest::SetStartupId { startup_id } => {
                let surface_id = this.get(client)?.surface_ref.id();
                println!("startup_id for surface {} = '{}'", surface_id, startup_id);
            }
            ZauraSurfaceRequest::SetApplicationId { application_id } => {
                let surface_id = this.get(client)?.surface_ref.id();
                println!("application_id for surface {} = '{}'", surface_id, application_id);
            }
            ZauraSurfaceRequest::SetClientSurfaceId { .. } => {}
            ZauraSurfaceRequest::SetOcclusionTracking => {}
            ZauraSurfaceRequest::UnsetOcclusionTracking { .. } => {}
        }
        Ok(())
    }
}

struct AuraOutput;

impl AuraOutput {
    pub fn new() -> Self {
        Self
    }
}

impl RequestReceiver<ZauraOutput> for AuraOutput {
    fn receive(
        _this: ObjectRef<Self>,
        request: ZauraOutputRequest,
        _client: &mut Client,
    ) -> Result<(), Error> {
        match request {}
    }
}
