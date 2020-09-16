// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    // TODO(51770): FuchsiaInstallPlan should be shared with omaha-client.
    super::install_plan::FuchsiaInstallPlan,
    crate::{cache::Cache, resolver::Resolver, updater::Updater},
    anyhow::Context,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io::DirectoryMarker,
    futures::future::BoxFuture,
    futures::prelude::*,
    omaha_client::installer::{Installer, ProgressObserver},
    std::sync::Arc,
    thiserror::Error,
};

/// An Omaha Installer implementation that uses the `isolated-swd` Updater to perform the OTA
/// installation.
/// This Installer implementation does not reboot when `perform_reboot` is called, as the caller of
/// `isolated-swd` is expected to do that.
pub struct IsolatedInstaller {
    blobfs: Option<ClientEnd<DirectoryMarker>>,
    paver_connector: Option<ClientEnd<DirectoryMarker>>,
    cache: Arc<Cache>,
    resolver: Arc<Resolver>,
    board_name: String,
    updater_url: String,
}

impl IsolatedInstaller {
    pub fn new(
        blobfs: ClientEnd<DirectoryMarker>,
        paver_connector: ClientEnd<DirectoryMarker>,
        cache: Arc<Cache>,
        resolver: Arc<Resolver>,
        board_name: String,
        updater_url: String,
    ) -> Self {
        IsolatedInstaller {
            blobfs: Some(blobfs),
            paver_connector: Some(paver_connector),
            cache,
            resolver,
            board_name,
            updater_url: updater_url,
        }
    }
}

#[derive(Debug, Error)]
pub enum IsolatedInstallError {
    #[error("running updater failed")]
    Failure(#[source] anyhow::Error),

    #[error("can only run the installer once")]
    AlreadyRun,
}

impl Installer for IsolatedInstaller {
    type InstallPlan = FuchsiaInstallPlan;
    type Error = IsolatedInstallError;

    fn perform_install<'a>(
        &'a mut self,
        install_plan: &FuchsiaInstallPlan,
        observer: Option<&'a dyn ProgressObserver>,
    ) -> BoxFuture<'_, Result<(), IsolatedInstallError>> {
        if let Some(o) = observer.as_ref() {
            o.receive_progress(None, 0., None, None);
        }

        if self.blobfs.is_none() {
            return async move { Err(IsolatedInstallError::AlreadyRun) }.boxed();
        }

        let update_result = Updater::launch_with_components(
            self.blobfs.take().unwrap(),
            self.paver_connector.take().unwrap(),
            Arc::clone(&self.cache),
            Arc::clone(&self.resolver),
            &self.board_name,
            Some(install_plan.url.to_string()),
            &self.updater_url,
        );
        async move {
            let result = update_result
                .await
                .map_err(IsolatedInstallError::Failure)?
                .ok()
                .context("Running the updater")
                .map_err(IsolatedInstallError::Failure);
            if let Some(o) = observer.as_ref() {
                o.receive_progress(None, 1., None, None);
            }
            result
        }
        .boxed()
    }

    fn perform_reboot(&mut self) -> BoxFuture<'_, Result<(), anyhow::Error>> {
        // We don't actually reboot here. The caller of isolated-swd is responsible for performing
        // a reboot after the update is installed.
        // Tell Omaha that the reboot was successful so that it finishes the update check
        // and omaha::install_update() can return.
        async move { Ok(()) }.boxed()
    }
}
