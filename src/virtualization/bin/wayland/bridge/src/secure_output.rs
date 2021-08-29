// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::client::Client,
    crate::compositor::Surface,
    crate::object::{NewObjectExt, ObjectRef, RequestReceiver},
    anyhow::Error,
    fuchsia_wayland_core as wl,
    zcr_secure_output_v1::{
        ZcrSecureOutputV1, ZcrSecureOutputV1Request, ZcrSecurityV1, ZcrSecurityV1Request,
    },
};

/// An implementation of the zcr_secure_output_v1 global.
pub struct SecureOutput;

impl SecureOutput {
    /// Creates a new `SecureOutput`.
    pub fn new() -> Self {
        SecureOutput
    }
}

impl RequestReceiver<ZcrSecureOutputV1> for SecureOutput {
    fn receive(
        this: ObjectRef<Self>,
        request: ZcrSecureOutputV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZcrSecureOutputV1Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZcrSecureOutputV1Request::GetSecurity { id, surface } => {
                id.implement(client, Security::new(surface))?;
            }
        }
        Ok(())
    }
}

struct Security {
    _surface_ref: ObjectRef<Surface>,
}

impl Security {
    pub fn new(surface: wl::ObjectId) -> Self {
        Security { _surface_ref: surface.into() }
    }
}

impl RequestReceiver<ZcrSecurityV1> for Security {
    fn receive(
        this: ObjectRef<Self>,
        request: ZcrSecurityV1Request,
        client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            ZcrSecurityV1Request::Destroy => {
                client.delete_id(this.id())?;
            }
            ZcrSecurityV1Request::OnlyVisibleOnSecureOutput => {}
        }
        Ok(())
    }
}
