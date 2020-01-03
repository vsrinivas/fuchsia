// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module provides common constants and helpers to assist in the unit-testing of other
//! modules within the crate.

use crate::common::AccountLifetime;
use account_common::{LocalAccountId, LocalPersonaId};
use fidl::endpoints::{create_endpoints, create_proxy_and_stream};
use fidl_fuchsia_auth::AppConfig;
use fidl_fuchsia_identity_account::Error;
use fidl_fuchsia_identity_internal::{
    AccountHandlerContextMarker, AccountHandlerContextProxy, AccountHandlerContextRequest,
    AccountHandlerContextRequestStream,
};
use fuchsia_async as fasync;
use futures::prelude::*;
use lazy_static::lazy_static;
use log::error;
use std::path::PathBuf;
use std::sync::Arc;
use tempfile::TempDir;

lazy_static! {
    pub static ref TEST_ACCOUNT_ID: LocalAccountId = LocalAccountId::new(111111);
    pub static ref TEST_PERSONA_ID: LocalPersonaId = LocalPersonaId::new(222222);
}

pub static TEST_APPLICATION_URL: &str = "test_app_url";

pub static TEST_ACCOUNT_ID_UINT: u64 = 111111;

// TODO(jsankey): If fidl calls ever accept non-mutable structs, move this to a lazy_static.
// Currently FIDL requires mutable access to a type that doesn't support clone, so we just create a
// fresh instance each time.
pub fn create_dummy_app_config() -> AppConfig {
    AppConfig {
        auth_provider_type: "dummy_auth_provider".to_string(),
        client_id: None,
        client_secret: None,
        redirect_uri: None,
    }
}

pub struct TempLocation {
    /// A fresh temp directory that will be deleted when this object is dropped.
    _dir: TempDir,
    /// A path to an existing temp dir.
    pub path: PathBuf,
}

impl TempLocation {
    /// Return a writable, temporary location and optionally create it as a directory.
    pub fn new() -> TempLocation {
        let dir = TempDir::new().unwrap();
        let path = dir.path().to_path_buf();
        TempLocation { _dir: dir, path }
    }

    /// Returns a path to a static test path inside the temporary location which does not exist by
    /// default.
    pub fn test_path(&self) -> PathBuf {
        self.path.join("test_path")
    }

    /// Returns a persistent AccountLifetime with the path set to this TempLocation's path.
    pub fn to_persistent_lifetime(&self) -> AccountLifetime {
        AccountLifetime::Persistent { account_dir: self.path.clone() }
    }
}

/// A fake meant for tests which rely on an AccountHandlerContext, but the context itself
/// isn't under test. As opposed to the real type, this doesn't depend on any other
/// components. Panics when getting unexpected messages or args, as defined by the implementation.
pub struct FakeAccountHandlerContext {}

impl FakeAccountHandlerContext {
    /// Creates new fake account handler context
    pub fn new() -> Self {
        Self {}
    }

    /// Asynchronously handles the supplied stream of `AccountHandlerContextRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountHandlerContextRequestStream,
    ) -> Result<(), fidl::Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    /// Asynchronously handles a single `AccountHandlerContextRequest`.
    async fn handle_request(&self, req: AccountHandlerContextRequest) -> Result<(), fidl::Error> {
        match req {
            AccountHandlerContextRequest::GetOauth { responder, .. } => {
                responder.send(&mut Err(Error::Internal))
            }
            AccountHandlerContextRequest::GetOpenIdConnect { responder, .. } => {
                responder.send(&mut Err(Error::Internal))
            }
            AccountHandlerContextRequest::GetOauthOpenIdConnect { responder, .. } => {
                responder.send(&mut Err(Error::Internal))
            }
        }
    }
}

/// Creates a new `AccountHandlerContext` channel, spawns a task to handle requests received on
/// this channel using the supplied `FakeAccountHandlerContext`, and returns the
/// `AccountHandlerContextProxy`.
pub fn spawn_context_channel(
    context: Arc<FakeAccountHandlerContext>,
) -> AccountHandlerContextProxy {
    let (proxy, stream) = create_proxy_and_stream::<AccountHandlerContextMarker>().unwrap();
    let context_clone = Arc::clone(&context);
    fasync::spawn(async move {
        context_clone
            .handle_requests_from_stream(stream)
            .await
            .unwrap_or_else(|err| error!("Error handling FakeAccountHandlerContext: {:?}", err))
    });
    proxy
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_context_fake() {
        let fake_context = Arc::new(FakeAccountHandlerContext::new());
        let proxy = spawn_context_channel(fake_context.clone());
        let (_, ap_server_end) = create_endpoints().expect("failed creating channel pair");
        assert_eq!(
            proxy.get_oauth("dummy_auth_provider", ap_server_end).await.unwrap(),
            Err(Error::Internal)
        );
    }
}
