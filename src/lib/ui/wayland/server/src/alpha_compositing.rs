// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::compositor::Surface,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    anyhow::Error,
    fuchsia_wayland_core as wl,
    zcr_alpha_compositing_v1::{
        ZcrAlphaCompositingV1, ZcrAlphaCompositingV1Request, ZcrBlendingV1, ZcrBlendingV1Request,
    },
};

/// An implementation of the zcr_alpha_compositing_v1 global.
pub struct AlphaCompositing;

impl AlphaCompositing {
    /// Creates a new `AlphaCompositing`.
    pub fn new() -> Self {
        AlphaCompositing
    }
}

impl RequestReceiver<ZcrAlphaCompositingV1> for AlphaCompositing {
    fn receive(
        this: ObjectRef<Self>,
        request: ZcrAlphaCompositingV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZcrAlphaCompositingV1Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZcrAlphaCompositingV1Request::GetBlending { id, surface } => {
                id.implement(client, AlphaBlending::new(surface))?;
            }
        }
        Ok(())
    }
}

struct AlphaBlending {
    _surface_ref: ObjectRef<Surface>,
}

impl AlphaBlending {
    pub fn new(surface: wl::ObjectId) -> Self {
        AlphaBlending { _surface_ref: surface.into() }
    }
}

impl RequestReceiver<ZcrBlendingV1> for AlphaBlending {
    fn receive(
        this: ObjectRef<Self>,
        request: ZcrBlendingV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZcrBlendingV1Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZcrBlendingV1Request::SetBlending { .. } => {}
            ZcrBlendingV1Request::SetAlpha { .. } => {}
        }
        Ok(())
    }
}
