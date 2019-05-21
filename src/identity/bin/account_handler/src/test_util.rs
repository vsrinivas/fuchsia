// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module provides common constants and helpers to assist in the unit-testing of other
//! modules within the crate.

use account_common::{LocalAccountId, LocalPersonaId};
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_auth::AppConfig;
use fidl_fuchsia_auth_account::Status;
use fidl_fuchsia_auth_account_internal::{
    AccountHandlerContextMarker, AccountHandlerContextRequest, AccountHandlerContextRequestStream,
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
        while let Some(req) = await!(stream.try_next())? {
            await!(self.handle_request(req))?;
        }
        Ok(())
    }

    /// Asynchronously handles a single `AccountHandlerContextRequest`.
    async fn handle_request(&self, req: AccountHandlerContextRequest) -> Result<(), fidl::Error> {
        match req {
            AccountHandlerContextRequest::GetAuthProvider { responder, .. } => {
                responder.send(Status::InternalError)
            }
        }
    }
}

/// Creates a new `AccountHandlerContext` channel, spawns a task to handle requests received on
/// this channel using the supplied `FakeAccountHandlerContext`, and returns the `ClientEnd`.
pub fn spawn_context_channel(
    context: Arc<FakeAccountHandlerContext>,
) -> ClientEnd<AccountHandlerContextMarker> {
    let (client_end, server_end) = create_endpoints().unwrap();
    let request_stream = server_end.into_stream().unwrap();
    let context_clone = Arc::clone(&context);
    fasync::spawn(async move {
        await!(context_clone.handle_requests_from_stream(request_stream))
            .unwrap_or_else(|err| error!("Error handling FakeAccountHandlerContext: {:?}", err))
    });
    client_end
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_until_stalled(test)]
    async fn test_context_fake() {
        let fake_context = Arc::new(FakeAccountHandlerContext::new());
        let client_end = spawn_context_channel(fake_context.clone());
        let proxy = client_end.into_proxy().unwrap();
        let (_, ap_server_end) = create_endpoints().expect("failed creating channel pair");
        assert_eq!(
            await!(proxy.get_auth_provider("dummy_auth_provider", ap_server_end)).unwrap(),
            Status::InternalError
        );
    }
}
