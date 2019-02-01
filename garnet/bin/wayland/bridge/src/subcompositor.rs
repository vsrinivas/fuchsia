// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use wayland::{WlSubcompositor, WlSubcompositorRequest};

use crate::client::Client;
use crate::object::{ObjectRef, RequestReceiver};

/// An implementation of the wl_subcompositor global.
pub struct Subcompositor;

impl Subcompositor {
    /// Creates a new `Subcompositor`.
    pub fn new() -> Self {
        Subcompositor
    }
}

impl RequestReceiver<WlSubcompositor> for Subcompositor {
    fn receive(
        this: ObjectRef<Self>, request: WlSubcompositorRequest, client: &mut Client,
    ) -> Result<(), Error> {
        match request {
            WlSubcompositorRequest::Destroy => {
                client.delete_id(this.id())?;
            }
            WlSubcompositorRequest::GetSubsurface { .. } => {}
        }
        Ok(())
    }
}
