// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error, ResultExt},
    fdio,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_tee_manager::{ServiceProviderRequest, ServiceProviderRequestStream},
    fuchsia_async as fasync,
    futures::prelude::*,
    std::{fs, path::PathBuf},
};

/// `ServiceProviderServer` implements the fuchsia.tee.manager.ServiceProvider FIDL protocol.
pub struct ServiceProviderServer {
    storage_dir: PathBuf,
}

impl ServiceProviderServer {
    pub fn try_new(storage_dir: PathBuf) -> Result<Self, Error> {
        fs::create_dir_all(&storage_dir)?;
        Ok(Self { storage_dir })
    }

    pub async fn serve(self, chan: fasync::Channel) -> Result<(), Error> {
        let mut request_stream = ServiceProviderRequestStream::from_channel(chan);

        while let Some(request) = await!(request_stream.try_next())
            .context("Error receiving ServiceProviderRequestStream message")?
        {
            let ServiceProviderRequest::RequestPersistentStorage { dir, .. } = request;

            let storage_dir_str =
                self.storage_dir.to_str().ok_or_else(|| format_err!("Invalid storage path"))?;
            fdio::service_connect(storage_dir_str, dir.into_channel()).with_context(|e| {
                format!(
                    "Failed to connect to storage directory ({}) service: {}",
                    storage_dir_str, e
                )
            })?;
        }

        Ok(())
    }
}
