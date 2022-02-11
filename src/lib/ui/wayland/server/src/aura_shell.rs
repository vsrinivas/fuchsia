// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::compositor::Surface,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    crate::output::Output,
    anyhow::Error,
    fidl_fuchsia_ui_gfx::DisplayInfo,
    fuchsia_wayland_core as wl,
    zaura_shell_server_protocol::{
        zaura_output, ZauraOutput, ZauraOutputRequest, ZauraShell, ZauraShellRequest, ZauraSurface,
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
            ZauraShellRequest::GetAuraOutput { id, output } => {
                let aura_output = AuraOutput::new();
                let aura_output_ref = id.implement(client, aura_output)?;
                let output: ObjectRef<Output> = output.into();
                output.get_mut(client)?.set_aura_output(aura_output_ref);
                let display_info = client.display().display_info();
                Output::post_display_info(output, client, &display_info)?;
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
            ZauraSurfaceRequest::SetParent { parent, x, y } => {
                let maybe_parent_ref = if parent != 0 {
                    let parent_ref: ObjectRef<AuraSurface> = parent.into();
                    Some(parent_ref.get(client)?.surface_ref)
                } else {
                    None
                };
                let surface_ref = this.get(client)?.surface_ref;
                let surface = surface_ref.get_mut(client)?;
                surface.set_parent_and_offset(maybe_parent_ref, x, y);
            }
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

pub struct AuraOutput;

impl AuraOutput {
    pub fn new() -> Self {
        Self
    }

    pub fn post_display_info(
        this: ObjectRef<Self>,
        client: &Client,
        _display_info: &DisplayInfo,
    ) -> Result<(), Error> {
        client.event_queue().post(
            this.id(),
            zaura_output::Event::Scale {
                flags: zaura_output::ScaleProperty::Current
                    | zaura_output::ScaleProperty::Preferred,
                // Use scale factor 1.0.
                scale: zaura_output::ScaleFactor::_1000,
            },
        )?;
        client.event_queue().post(
            this.id(),
            zaura_output::Event::Connection { connection: zaura_output::ConnectionType::Internal },
        )?;
        client.event_queue().post(
            this.id(),
            zaura_output::Event::DeviceScaleFactor { scale: zaura_output::ScaleFactor::_1000 },
        )?;
        Ok(())
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
