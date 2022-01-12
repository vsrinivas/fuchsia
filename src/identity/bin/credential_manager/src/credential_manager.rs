// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use fidl_fuchsia_identity_credential::{
    self as fcred, CredentialManagerRequest, CredentialManagerRequestStream,
};
use futures::prelude::*;
use log::error;

pub struct CredentialManager {}

impl CredentialManager {
    pub fn new() -> Self {
        Self {}
    }

    /// Serially process a stream of incoming CredentialManager FIDL requests.
    pub async fn handle_requests_for_stream(
        &self,
        mut request_stream: CredentialManagerRequestStream,
    ) {
        while let Some(request) = request_stream.try_next().await.expect("read request") {
            self.handle_request(request)
                .unwrap_or_else(|e| {
                    error!("error handling fidl request: {:#}", anyhow!(e));
                })
                .await
        }
    }

    /// Process a single CredentialManager FIDL request and send a reply.
    async fn handle_request(&self, request: CredentialManagerRequest) -> Result<(), Error> {
        match request {
            CredentialManagerRequest::AddCredential { params, responder } => {
                let mut resp = self.add_credential(&params).await;
                responder.send(&mut resp).context("sending AddCredential response")?;
            }
            CredentialManagerRequest::RemoveCredential { label, responder } => {
                let mut resp = self.remove_credential(label).await;
                responder.send(&mut resp).context("sending RemoveLabel response")?;
            }
            CredentialManagerRequest::CheckCredential { params, responder } => {
                let mut resp = self.check_credential(&params).await;
                responder.send(&mut resp).context("sending CheckCredential response")?;
            }
        }
        Ok(())
    }

    async fn add_credential(
        &self,
        _params: &fcred::AddCredentialParams,
    ) -> Result<u64, fcred::CredentialError> {
        Err(fcred::CredentialError::UnsupportedOperation)
    }

    async fn remove_credential(&self, _label: u64) -> Result<(), fcred::CredentialError> {
        Err(fcred::CredentialError::UnsupportedOperation)
    }

    async fn check_credential(
        &self,
        _params: &fcred::CheckCredentialParams,
    ) -> Result<fcred::CheckCredentialResponse, fcred::CredentialError> {
        Err(fcred::CredentialError::UnsupportedOperation)
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia::test]
    async fn test_add_credential() {
        let cred_manager = CredentialManager::new();
        let params = fcred::AddCredentialParams { ..fcred::AddCredentialParams::EMPTY };
        assert_eq!(
            cred_manager.add_credential(&params).await,
            Err(fcred::CredentialError::UnsupportedOperation)
        );
    }

    #[fuchsia::test]
    async fn test_remove_credential() {
        let cred_manager = CredentialManager::new();
        assert_eq!(
            cred_manager.remove_credential(1).await,
            Err(fcred::CredentialError::UnsupportedOperation)
        );
    }

    #[fuchsia::test]
    async fn test_check_credential() {
        let cred_manager = CredentialManager::new();
        let params = fcred::CheckCredentialParams { ..fcred::CheckCredentialParams::EMPTY };
        assert_eq!(
            cred_manager.check_credential(&params).await,
            Err(fcred::CredentialError::UnsupportedOperation)
        );
    }
}
