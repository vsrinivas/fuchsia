// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::update::{UpdateAttempt, UpdateHistory},
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_update_installer::{InstallerRequest, InstallerRequestStream, UpdateResult},
    fuchsia_component::server::{ServiceFs, ServiceObjLocal},
    fuchsia_syslog::fx_log_err,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
};

pub enum IncomingService {
    Installer(InstallerRequestStream),
}

pub struct FidlServer {
    history: Arc<Mutex<UpdateHistory>>,
}

impl FidlServer {
    pub fn new(history: Arc<Mutex<UpdateHistory>>) -> Self {
        Self { history }
    }

    /// Runs the FIDL Server.
    pub async fn run(self, mut fs: ServiceFs<ServiceObjLocal<'_, IncomingService>>) {
        fs.dir("svc").add_fidl_service(IncomingService::Installer);

        // Handles each client connection concurrently.
        fs.for_each_concurrent(None, |incoming_service| {
            self.handle_client(incoming_service).unwrap_or_else(|e| {
                fx_log_err!("error encountered while handling client: {:#}", anyhow!(e))
            })
        })
        .await
    }

    /// Handles an incoming FIDL connection from a client.
    async fn handle_client(&self, incoming_service: IncomingService) -> Result<(), Error> {
        match incoming_service {
            IncomingService::Installer(mut stream) => {
                while let Some(request) =
                    stream.try_next().await.context("error receiving Installer request")?
                {
                    self.handle_installer_request(request).await?;
                }
            }
        }
        Ok(())
    }

    /// Handles fuchsia.update.update.Installer requests.
    async fn handle_installer_request(&self, request: InstallerRequest) -> Result<(), Error> {
        match request {
            InstallerRequest::GetLastUpdateResult { responder } => {
                let history = self.history.lock();
                let last_result = into_update_result(history.last_update_attempt());
                responder.send(last_result)?;
            }
            InstallerRequest::GetUpdateResult { attempt_id, responder } => {
                let history = self.history.lock();
                let result = into_update_result(history.update_attempt(attempt_id));
                responder.send(result)?;
            }
            // TODO(fxb/55408): Implement StartUpdate and MonitorUpdate.
            InstallerRequest::StartUpdate { .. } => panic!("endpoint not implemented"),
            InstallerRequest::MonitorUpdate { .. } => panic!("endpoint not implemented"),
        }
        Ok(())
    }
}

fn into_update_result(attempt: Option<&UpdateAttempt>) -> UpdateResult {
    match attempt {
        None => UpdateResult { attempt_id: None, url: None, options: None, state: None },
        Some(attempt) => attempt.into(),
    }
}
