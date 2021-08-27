// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod command;
mod status;

use crate::cr50::{
    command::{
        ccd::{CcdCommand, CcdGetInfoResponse, CcdRequest},
        TpmCommand,
    },
    status::ExecuteError,
};
use anyhow::{Context, Error};
use fidl_fuchsia_tpm::TpmDeviceProxy;
use fidl_fuchsia_tpm_cr50::{
    CcdCapability, CcdFlags, CcdIndicator, CcdInfo, CcdState, Cr50Rc, Cr50Request,
    Cr50RequestStream, Cr50Status,
};
use fuchsia_syslog::fx_log_warn;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use std::sync::Arc;

pub struct Cr50 {
    proxy: TpmDeviceProxy,
}

impl Cr50 {
    pub fn new(proxy: TpmDeviceProxy) -> Arc<Self> {
        Arc::new(Cr50 { proxy })
    }

    pub async fn handle_stream(&self, mut stream: Cr50RequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("Reading from stream")? {
            match request {
                Cr50Request::CcdGetInfo { responder } => match self.get_info().await {
                    Ok(info) => responder
                        .send(&mut Ok((Cr50Rc::Cr50(Cr50Status::Success), Some(Box::new(info))))),
                    Err(ExecuteError::Tpm(status)) => {
                        responder.send(&mut Ok((status.into(), None)))
                    }
                    Err(ExecuteError::Other(e)) => {
                        fx_log_warn!("Error while executing GetInfo: {:?}", e);
                        responder.send(&mut Err(zx::Status::INTERNAL.into_raw()))
                    }
                }
                .context("Replying to request")?,
            };
        }

        Ok(())
    }

    pub async fn get_info(&self) -> Result<CcdInfo, ExecuteError> {
        let req = CcdRequest::<CcdGetInfoResponse>::new(CcdCommand::GetInfo);

        let result = req.execute(&self.proxy).await?;

        let mut caps = Vec::new();
        let mut i = 0;
        while let Some(cap) = CcdCapability::from_primitive(i) {
            caps.push(result.get_capability(cap));
            i += 1;
        }

        Ok(CcdInfo {
            capabilities: caps,
            flags: CcdFlags::from_bits_truncate(result.ccd_flags),
            state: CcdState::from_primitive(result.ccd_state).unwrap(),
            indicator: CcdIndicator::from_bits_truncate(result.ccd_indicator_bitmap),
            force_disabled: result.ccd_forced_disabled > 0,
        })
    }
}
