// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::results,
    failure::{bail, Error, ResultExt},
    fidl_test_inspect_validate as validate,
    fuchsia_component::client as fclient,
    fuchsia_zircon::{self as zx},
    log::*,
};

use fuchsia_zircon::Vmo;

pub struct Blocks {}

impl Blocks {}

pub struct Puppet {
    vmo: Vmo,
}

impl Puppet {
    pub fn apply(&mut self, _actions: &[validate::Action], _results: &mut results::Results) {}

    pub fn vmo_blocks(&self, _results: &mut results::Results) -> Result<Blocks, Error> {
        let mut header_bytes: [u8; 16] = [0; 16];
        self.vmo.read(&mut header_bytes, 0)?;

        Ok(Blocks {})
    }

    pub async fn connect(
        server_url: &str,
        results: &mut results::Results,
    ) -> Result<Puppet, Error> {
        let launcher = fclient::launcher().context("Failed to open launcher service")?;
        let app = fclient::launch(&launcher, server_url.to_owned(), None)
            .context(format!("Failed to launch Validator puppet {}", server_url))?;
        let puppet_fidl = app
            .connect_to_service::<validate::ValidateMarker>()
            .context("Failed to connect to validate puppet")?;
        let params = validate::InitializationParams { vmo_size: Some(4096) };
        let out = puppet_fidl.initialize(params).await.context("Calling vmo init")?;
        info!("Out from initialize: {:?}", out);
        let mut handle: Option<zx::Handle> = None;
        if let (Some(out_handle), _) = out {
            handle = Some(out_handle);
        } else {
            results.error("Didn't get a VMO handle".into());
        }
        if let Some(unwrapped_handle) = handle {
            return Ok(Puppet { vmo: Vmo::from(unwrapped_handle) });
        } else {
            results.error("Didn't unwrap a handle".into());
        }
        bail!("Failed to connect; see JSON output for details");
    }
}
