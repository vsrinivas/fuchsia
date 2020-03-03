// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Result, SessionId};
use fuchsia_zircon as zx;
use zx::AsHandleRef;

/// A universally unique id.
#[derive(Debug)]
pub struct Id {
    id: SessionId,
    id_handle: zx::Event,
}

impl Id {
    pub fn new() -> Result<Self> {
        let id_handle = zx::Event::create()?;
        let id = id_handle.get_koid()?.raw_koid();

        Ok(Self { id, id_handle })
    }

    pub fn get(&self) -> SessionId {
        self.id
    }
}
