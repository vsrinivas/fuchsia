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
            pinweaver::{
                GetLogEntryData, PinweaverGetLog, PinweaverInsertLeaf, PinweaverLogReplay,
                PinweaverRemoveLeaf, PinweaverResetTree, PinweaverTryAuth, PROTOCOL_VERSION,
            },
            wp::WpInfoRequest,
            Serializable, TpmCommand,
        },
        status::{ExecuteError, TpmStatus},
    },
    power_button::PowerButton,
    util::Serializer,
};
use anyhow::{anyhow, Context, Error};
use fidl::endpoints::RequestStream;
use fidl_fuchsia_tpm::TpmDeviceProxy;
use fidl_fuchsia_tpm_cr50::{
    CcdCapability, CcdFlags, CcdIndicator, CcdInfo, CcdState, Cr50Rc, Cr50Request,
    Cr50RequestStream, Cr50Status, EntryData, InsertLeafResponse, LogEntry, LogReplayResponse,
    MessageType, PhysicalPresenceEvent, PhysicalPresenceNotifierMarker, PhysicalPresenceState,
    PinWeaverError, PinWeaverRequest, PinWeaverRequestStream, TryAuthFailed, TryAuthRateLimited,
    TryAuthResponse, TryAuthSuccess, WpState,
};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use std::sync::Arc;
use tracing::warn;

pub struct Cr50 {
    proxy: TpmDeviceProxy,
    power_button: Option<Arc<PowerButton>>,
}

impl Cr50 {
    pub fn new(proxy: TpmDeviceProxy, power_button: Option<Arc<PowerButton>>) -> Arc<Self> {
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
                warn!("Error while executing {}: {:?}", cmd, e);
                Err(zx::Status::INTERNAL.into_raw())
            }
        }
    }

    pub async fn handle_cr50_stream(&self, mut stream: Cr50RequestStream) -> Result<(), Error> {
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

    pub async fn handle_pinweaver_stream(
        &self,
        mut stream: PinWeaverRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                PinWeaverRequest::GetVersion { responder } => {
                    responder.send(PROTOCOL_VERSION).context("Failed replying to request")?;
                }
                PinWeaverRequest::ResetTree { bits_per_level, height, responder } => {
                    let request = PinweaverResetTree::new(bits_per_level, height);
                    // TODO(fxbug.dev/90618): what is the correct way to handle errors in the
                    // underlying TPM transport?
                    let result =
                        request.execute(&self.proxy).await.context("Executing TPM command")?;
                    responder
                        .send(&mut result.ok().map(|_| result.root))
                        .context("Replying to request")?;
                }
                PinWeaverRequest::InsertLeaf { params, responder } => {
                    let request = match PinweaverInsertLeaf::new(params) {
                        Ok(req) => req,
                        Err(e) => {
                            responder.send(&mut Err(e)).context("Replying to request")?;
                            continue;
                        }
                    };

                    let result =
                        request.execute(&self.proxy).await.context("Executing TPM command")?;
                    let mut fidl_result = result.ok().map(|response| {
                        let mut table = InsertLeafResponse::EMPTY;
                        table.root_hash = Some(response.root);
                        let data = response.data.as_ref().unwrap();
                        table.mac = Some(data.leaf_data.hmac);
                        // cred metadata is just the whole unimported_leaf_data.
                        let mut serializer = Serializer::new();
                        data.leaf_data.serialize(&mut serializer);
                        table.cred_metadata = Some(serializer.into_vec());
                        table
                    });

                    responder.send(&mut fidl_result).context("Replying to request")?;
                }
                PinWeaverRequest::RemoveLeaf { params, responder } => {
                    let request = match PinweaverRemoveLeaf::new(params) {
                        Ok(req) => req,
                        Err(e) => {
                            responder.send(&mut Err(e)).context("Replying to request")?;
                            continue;
                        }
                    };

                    let result =
                        request.execute(&self.proxy).await.context("Executing TPM command")?;
                    responder
                        .send(&mut result.ok().map(|_| result.root))
                        .context("Replying to request")?;
                }
                PinWeaverRequest::TryAuth { params, responder } => {
                    let request = match PinweaverTryAuth::new(params) {
                        Ok(req) => req,
                        Err(e) => {
                            responder.send(&mut Err(e)).context("Replying to request")?;
                            continue;
                        }
                    };

                    let result =
                        request.execute(&self.proxy).await.context("Executing TPM command")?;

                    let mut fidl_result = match result.ok() {
                        Ok(_) => {
                            let mut success = TryAuthSuccess::EMPTY;
                            let data = result.data.as_ref().unwrap();
                            success.root_hash = Some(result.root);
                            success.he_secret = Some(data.high_entropy_secret.to_vec());
                            success.reset_secret = Some(data.reset_secret.to_vec());

                            // cred metadata is just the whole unimported_leaf_data.
                            let mut serializer = Serializer::new();
                            data.unimported_leaf_data.serialize(&mut serializer);
                            success.cred_metadata = Some(serializer.into_vec());

                            success.mac = Some(data.unimported_leaf_data.hmac);
                            Ok(TryAuthResponse::Success(success))
                        }
                        Err(PinWeaverError::RateLimitReached) => {
                            let mut rate_limited = TryAuthRateLimited::EMPTY;
                            rate_limited.time_to_wait =
                                Some(result.data.as_ref().unwrap().time_diff.into());
                            Ok(TryAuthResponse::RateLimited(rate_limited))
                        }
                        Err(PinWeaverError::LowentAuthFailed) => {
                            let mut auth_failed = TryAuthFailed::EMPTY;
                            let data = result.data.as_ref().unwrap();
                            auth_failed.root_hash = Some(result.root);
                            // cred metadata is just the whole unimported_leaf_data.
                            let mut serializer = Serializer::new();
                            data.unimported_leaf_data.serialize(&mut serializer);
                            auth_failed.cred_metadata = Some(serializer.into_vec());
                            auth_failed.mac = Some(data.unimported_leaf_data.hmac);
                            Ok(TryAuthResponse::Failed(auth_failed))
                        }
                        Err(e) => Err(e),
                    };

                    responder.send(&mut fidl_result).context("Replying to request")?;
                }
                PinWeaverRequest::GetLog { root_hash, responder } => {
                    let request = match PinweaverGetLog::new(root_hash) {
                        Ok(req) => req,
                        Err(e) => {
                            responder.send(&mut Err(e)).context("Replying to request")?;
                            continue;
                        }
                    };

                    let exec_result =
                        request.execute(&self.proxy).await.context("Executing TPM command")?;
                    let mut result = match exec_result.ok() {
                        Ok(_) => Ok(exec_result
                            .data
                            .ok_or(anyhow::anyhow!("No data in GetLog?"))?
                            .log_entries
                            .into_iter()
                            .map(|v| {
                                // Unpack the TPM type into the FIDL type.
                                let mut entry = LogEntry::EMPTY;
                                entry.root_hash = Some(v.root);
                                entry.label = Some(v.label);
                                let mut entry_data: EntryData = EntryData::EMPTY;
                                entry.message_type = Some(match v.action {
                                    GetLogEntryData::InsertLeaf(hash) => {
                                        entry_data.leaf_hmac = Some(hash);
                                        MessageType::InsertLeaf
                                    }
                                    GetLogEntryData::RemoveLeaf(rc) => {
                                        entry_data.boot_count = Some(rc.boot_count);
                                        entry_data.timestamp = Some(rc.timer_value);
                                        entry_data.return_code = Some(rc.return_code);
                                        MessageType::RemoveLeaf
                                    }
                                    GetLogEntryData::TryAuth(rc) => {
                                        entry_data.boot_count = Some(rc.boot_count);
                                        entry_data.timestamp = Some(rc.timer_value);
                                        entry_data.return_code = Some(rc.return_code);
                                        MessageType::TryAuth
                                    }
                                    GetLogEntryData::ResetTree => MessageType::ResetTree,
                                });
                                entry.entry_data = Some(entry_data);
                                entry
                            })
                            .collect::<Vec<LogEntry>>()),
                        Err(e) => Err(e),
                    };

                    responder.send(&mut result).context("Replying to request")?;
                }
                PinWeaverRequest::LogReplay { params, responder } => {
                    let request = match PinweaverLogReplay::new(params) {
                        Ok(req) => req,
                        Err(e) => {
                            responder.send(&mut Err(e)).context("Replying to request")?;
                            continue;
                        }
                    };

                    let exec_result = request.execute(&self.proxy).await?;

                    let mut fidl_result = match exec_result.ok() {
                        Ok(_) => {
                            let mut success = LogReplayResponse::EMPTY;
                            let data = exec_result.data.as_ref().unwrap();
                            // cred metadata is just the whole unimported_leaf_data.
                            let mut serializer = Serializer::new();
                            data.unimported_leaf_data.serialize(&mut serializer);
                            success.cred_metadata = Some(serializer.into_vec());

                            success.leaf_hash = Some(data.unimported_leaf_data.hmac);
                            Ok(success)
                        }
                        Err(e) => Err(e),
                    };

                    responder.send(&mut fidl_result).context("Replying to request")?;
                }
            }
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

    /// Spawn a task that polls for physical presence check updates.
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
        let inhibitor =
            if let Some(power_button) = self.power_button.as_ref().map(|v| Arc::clone(v)) {
                // Inhibit the power button now so that if something goes wrong we can propagate the
                // error back to the client.
                Some(power_button.inhibit().await.context("Inhibiting power button")?)
            } else {
                // No power button inhibitor is available. Print out a warning but continue - the TPM
                // will do the presence check even if the AP powers off.
                warn!(
                    "Power button inhibitor is unavailable. Device may power off when physical \
                    presence check starts, check TPM console for physical presence status"
                );
                None
            };

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
                        warn!("Physical presence check failed: {:?}", e);
                        handle
                            .send_on_change(&mut PhysicalPresenceEvent::Err(
                                zx::Status::INTERNAL.into_raw(),
                            ))
                            .unwrap_or_else(|e| warn!("Error sending on change: {:?}", e));
                        return;
                    }
                };
                if Some(pp.get_state()) != last_pp {
                    last_pp = Some(pp.get_state());
                    handle
                        .send_on_change(&mut PhysicalPresenceEvent::State(pp.get_state()))
                        .unwrap_or_else(|e| warn!("Error sending on change: {:?}", e));
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

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_tpm::{TpmDeviceMarker, TpmDeviceRequest, TpmDeviceRequestStream};
    use fidl_fuchsia_tpm_cr50::PinWeaverMarker;

    struct FakeTpm {}

    impl FakeTpm {
        pub fn new() -> Arc<Self> {
            Arc::new(FakeTpm {})
        }

        async fn serve(self: Arc<Self>, mut stream: TpmDeviceRequestStream) {
            while let Some(req) = stream.try_next().await.expect("Getting requests") {
                match req {
                    TpmDeviceRequest::GetDeviceId { responder } => responder
                        .send(&mut Ok((0x1ae0, 0x0028, 0x00)))
                        .expect("Responding to request"),
                    TpmDeviceRequest::ExecuteVendorCommand { .. } => todo!(),
                    TpmDeviceRequest::ExecuteCommand { .. } => todo!(),
                }
            }
        }
    }

    #[fuchsia::test]
    async fn test_get_version() {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<TpmDeviceMarker>().unwrap();
        let tpm = FakeTpm::new();
        fasync::Task::spawn(tpm.serve(stream)).detach();

        let cr50 = Cr50::new(proxy, None);

        let (pinweaver, stream) =
            fidl::endpoints::create_proxy_and_stream::<PinWeaverMarker>().unwrap();
        fasync::Task::spawn(async move {
            cr50.handle_pinweaver_stream(stream).await.expect("Handle pinweaver stream ok");
        })
        .detach();

        assert_eq!(pinweaver.get_version().await.expect("Sending fidl request ok"), 1);
    }
}
