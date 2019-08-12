// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::auth_provider::{self, GoogleAuthProvider};
use crate::http::UrlLoaderHttpClient;
use crate::web::DefaultStandaloneWebFrame;
use failure::Error;
use fidl::endpoints::{create_proxy, ClientEnd};
use fidl_fuchsia_auth::{
    AuthProviderFactoryRequest, AuthProviderFactoryRequestStream, AuthProviderStatus,
};
use fidl_fuchsia_net_oldhttp::{HttpServiceMarker, UrlLoaderMarker};
use fidl_fuchsia_web::{ContextMarker, ContextProviderMarker, CreateContextParams, FrameMarker};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;

use futures::prelude::*;
use log::error;
use std::sync::Arc;

/// Implementation of the `AuthProviderFactory` FIDL protocol capable of
/// returning `AuthProvider` channels for a `GoogleAuthProvider`.
pub struct GoogleAuthProviderFactory {
    /// The GoogleAuthProvider vended through `GetAuthProvider` requests.
    google_auth_provider: Arc<GoogleAuthProvider<WebFrameSupplier, UrlLoaderHttpClient>>,
}

impl GoogleAuthProviderFactory {
    /// Create a new `GoogleAuthProviderFactory`
    pub fn new() -> Result<Self, Error> {
        let http_service = connect_to_service::<HttpServiceMarker>()?;
        let (url_loader, url_loader_service) = create_proxy::<UrlLoaderMarker>()?;
        http_service.create_url_loader(url_loader_service)?;
        let frame_supplier = WebFrameSupplier::new();
        let http_client = UrlLoaderHttpClient::new(url_loader);
        Ok(GoogleAuthProviderFactory {
            google_auth_provider: Arc::new(GoogleAuthProvider::new(frame_supplier, http_client)),
        })
    }

    /// Handle requests passed to the supplied stream.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AuthProviderFactoryRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            self.handle_request(request).await?;
        }
        Ok(())
    }

    /// Handle `AuthProviderFactoryRequest`.
    /// Spawn a new `AuthProvider` on every `GetAuthProvider` request
    /// to the `AuthProviderFactory` interface.
    async fn handle_request(&self, req: AuthProviderFactoryRequest) -> Result<(), Error> {
        let AuthProviderFactoryRequest::GetAuthProvider { auth_provider: server_end, responder } =
            req;
        let request_stream = server_end.into_stream()?;
        let auth_provider = Arc::clone(&self.google_auth_provider);
        fasync::spawn(async move {
            auth_provider
                .handle_requests_from_stream(request_stream)
                .await
                .unwrap_or_else(|e| error!("Error handling AuthProvider channel {:?}", e));
        });
        responder.send(AuthProviderStatus::Ok)?;
        Ok(())
    }
}

/// Struct that provides new web frames by connecting to a ContextProvider service.
struct WebFrameSupplier;

impl WebFrameSupplier {
    /// Create a new `WebFrameSupplier`
    fn new() -> Self {
        WebFrameSupplier {}
    }
}

impl auth_provider::WebFrameSupplier for WebFrameSupplier {
    type Frame = DefaultStandaloneWebFrame;
    fn new_standalone_frame(&self) -> Result<DefaultStandaloneWebFrame, failure::Error> {
        let context_provider = connect_to_service::<ContextProviderMarker>()?;
        let (context_proxy, context_server_end) = create_proxy::<ContextMarker>()?;

        // Get a handle to the incoming service directory to pass to the web browser.
        // TODO(satsukiu): create a method in fidl::endpoints to connect to ServiceFs instead.
        let (client, server) = zx::Channel::create()?;
        fdio::service_connect("/svc", server)?;
        let service_directory = ClientEnd::new(client);

        context_provider.create(
            CreateContextParams {
                service_directory: Some(service_directory),
                data_directory: None,
                user_agent_product: None,
                user_agent_version: None,
                remote_debugging_port: None,
            },
            context_server_end,
        )?;

        let (frame_proxy, frame_server_end) = create_proxy::<FrameMarker>()?;
        context_proxy.create_frame(frame_server_end)?;
        Ok(DefaultStandaloneWebFrame::new(context_proxy, frame_proxy))
    }
}
