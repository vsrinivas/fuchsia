// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod command;
mod status;

use crate::{
    cr50::{
        command::{
            ccd::{
                CcdCommand, CcdGetInfoResponse, CcdOpenResponse, CcdPhysicalPresenceResponse,
                CcdRequest,
            },
            wp::WpInfoRequest,
            TpmCommand,
        },
        status::{ExecuteError, TpmStatus},
    },
    power_button::PowerButton,
};
use anyhow::{anyhow, Context, Error};
use fidl::endpoints::RequestStream;
use fidl_fuchsia_tpm::TpmDeviceProxy;
use fidl_fuchsia_tpm_cr50::{
    CcdCapability, CcdFlags, CcdIndicator, CcdInfo, CcdState, Cr50Rc, Cr50Request,
    Cr50RequestStream, Cr50Status, PhysicalPresenceEvent, PhysicalPresenceNotifierMarker,
    PhysicalPresenceState, WpState,
};
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_warn;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use std::sync::Arc;

pub struct Cr50 {
    proxy: TpmDeviceProxy,
    power_button: Arc<PowerButton>,
}

impl Cr50 {
    pub fn new(proxy: TpmDeviceProxy, power_button: Arc<PowerButton>) -> Arc<Self> {
        Arc::new(Cr50 { proxy, power_button })
    }

    fn make_response<W>(
        cmd: &str,
        result: Result<W, ExecuteError>,
        default: W,
    ) -> Result<(Cr50Rc, W), i32> {
        match result {
            Ok(content) => Ok((Cr50Rc::Cr50(Cr50Status::Success), content)),
            Err(ExecuteError::Tpm(status)) => Ok((status.into(), default)),
            Err(ExecuteError::Other(e)) => {
                fx_log_warn!("Error while executing {}: {:?}", cmd, e);
                Err(zx::Status::INTERNAL.into_raw())
            }
        }
    }

    pub async fn handle_stream(&self, mut stream: Cr50RequestStream) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await.context("Reading from stream")? {
            match request {
                Cr50Request::CcdGetInfo { responder } => responder
                    .send(&mut Self::make_response(
                        "CcdGetInfo",
                        self.get_info().await.map(|v| Some(Box::new(v))),
                        None,
                    ))
                    .context("Replying to request")?,
                Cr50Request::WpGetState { responder } => responder
                    .send(&mut Self::make_response(
                        "WpGetState",
                        self.wp_get_state().await,
                        WpState::empty(),
                    ))
                    .context("Replying to request")?,
                Cr50Request::CcdLock { responder } => responder
                    .send(
                        &mut Self::make_response(
                            "CcdLock",
                            self.ccd_command(CcdCommand::Lock, None).await,
                            (),
                        )
                        .map(|(rc, _)| rc),
                    )
                    .context("Replying to request")?,
                Cr50Request::CcdOpen { password, responder } => responder
                    .send(&mut Self::make_response(
                        "CcdOpen",
                        self.handle_open_or_unlock(CcdCommand::Open, password).await.map(Some),
                        None,
                    ))
                    .context("Replying to request")?,
                Cr50Request::CcdUnlock { password, responder } => responder
                    .send(&mut Self::make_response(
                        "CcdUnlock",
                        self.handle_open_or_unlock(CcdCommand::Open, password).await.map(Some),
                        None,
                    ))
                    .context("Replying to request")?,
            };
        }

        Ok(())
    }

    async fn handle_open_or_unlock(
        &self,
        cmd: CcdCommand,
        password: Option<String>,
    ) -> Result<fidl::endpoints::ClientEnd<PhysicalPresenceNotifierMarker>, ExecuteError> {
        let poll_cmd = match cmd {
            CcdCommand::Open => CcdCommand::CmdPpPollOpen,
            CcdCommand::Unlock => CcdCommand::CmdPpPollUnlock,
            _ => panic!("Expected open or unlock"),
        };
        match self.ccd_command(cmd, password).await {
            Ok(()) | Err(ExecuteError::Tpm(TpmStatus(Cr50Rc::Cr50(Cr50Status::InProgress)))) => {
                // Need physical presence check. If no check is required (e.g. battery
                // disconnected), handle_physical_presence will indicate that to the client.
                self.handle_physical_presence(poll_cmd).await.map_err(ExecuteError::Other)
            }
            Err(e) => Err(e),
        }
    }

    /// Spawn a task that does polls for physical presence check updates.
    /// Returns a client which will receive events when physical presence check state
    /// changes.
    async fn handle_physical_presence(
        &self,
        cmd: CcdCommand,
    ) -> Result<fidl::endpoints::ClientEnd<PhysicalPresenceNotifierMarker>, anyhow::Error> {
        let (client, server) =
            fidl::endpoints::create_request_stream::<PhysicalPresenceNotifierMarker>()
                .context("Creating request stream")?;
        let proxy = self.proxy.clone();
        let power_button = Arc::clone(&self.power_button);
        // Inhibit the power button now so that if something goes wrong we can propagte the error
        // back to the client.
        let inhibitor = power_button.inhibit().await.context("Inhibiting power button")?;

        fasync::Task::spawn(async move {
            // Tie the lifetime of the inhibitor to this async task.
            let _inhibitor = inhibitor;
            let handle = server.control_handle();
            let mut last_pp = None;
            while last_pp != Some(PhysicalPresenceState::Done)
                && last_pp != Some(PhysicalPresenceState::Closed)
            {
                let request = CcdRequest::<CcdPhysicalPresenceResponse>::new(cmd);
                let pp = match request.execute(&proxy).await {
                    Ok(pp) => pp,
                    Err(e) => {
                        fx_log_warn!("Physical presence check failed: {:?}", e);
                        handle
                            .send_on_change(&mut PhysicalPresenceEvent::Err(
                                zx::Status::INTERNAL.into_raw(),
                            ))
                            .unwrap_or_else(|e| fx_log_warn!("Error sending on change: {:?}", e));
                        return;
                    }
                };
                if Some(pp.get_state()) != last_pp {
                    last_pp = Some(pp.get_state());
                    handle
                        .send_on_change(&mut PhysicalPresenceEvent::State(pp.get_state()))
                        .unwrap_or_else(|e| fx_log_warn!("Error sending on change: {:?}", e));
                }

                // Wait 10ms before checking again.
                fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(10))).await;
            }
        })
        .detach();

        Ok(client)
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

    pub async fn ccd_command(
        &self,
        cmd: CcdCommand,
        password: Option<String>,
    ) -> Result<(), ExecuteError> {
        let req: CcdRequest<CcdOpenResponse> = match password {
            None => CcdRequest::new(cmd),
            Some(password) => CcdRequest::new_with_password(cmd, &password)
                .map_err(|_| ExecuteError::Other(anyhow!("Invalid password")))?,
        };

        req.execute(&self.proxy).await?;

        Ok(())
    }

    pub async fn wp_get_state(&self) -> Result<WpState, ExecuteError> {
        let req = WpInfoRequest::new();
        let result = req.execute(&self.proxy).await?;
        return Ok(result.get_state());
    }
}
