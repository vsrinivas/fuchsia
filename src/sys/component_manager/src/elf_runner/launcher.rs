// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::builtin::process_launcher::ProcessLauncher,
    ::routing::config::RuntimeConfig,
    anyhow::{Context as _, Error},
    fidl_fuchsia_process as fproc, fuchsia_async as fasync,
    fuchsia_component::client,
    log::warn,
};
/// Connects to the appropriate `fuchsia.process.Launcher` service based on the options provided in
/// `ProcessLauncherConnector::new`.
///
/// This exists so that callers can make a new connection to `fuchsia.process.Launcher` for each use
/// because the service is stateful per connection, so it is not safe to share a connection between
/// multiple asynchronous process launchers.
///
/// If `Arguments.use_builtin_process_launcher` is true, this will connect to the built-in
/// `fuchsia.process.Launcher` service using the provided `ProcessLauncher`. Otherwise, this connects
/// to the launcher service under /svc in component_manager's namespace.
pub struct ProcessLauncherConnector {
    use_builtin: bool,
}

impl ProcessLauncherConnector {
    pub fn new(config: &RuntimeConfig) -> Self {
        Self { use_builtin: config.use_builtin_process_launcher }
    }

    // This only returns an error when opening connection to an external
    // Launcher service. If the built-in is used and fails to start it can only
    // be discovered by getting an error attempting to use the launcher.
    pub fn connect(&self) -> Result<fproc::LauncherProxy, Error> {
        let proxy = if self.use_builtin {
            let (proxy, stream) =
                fidl::endpoints::create_proxy_and_stream::<fproc::LauncherMarker>()?;
            fasync::Task::spawn(async move {
                let result = ProcessLauncher::serve(stream).await;
                if let Err(e) = result {
                    warn!("ProcessLauncherConnector.connect failed: {}", e);
                }
            })
            .detach();
            proxy
        } else {
            client::connect_to_protocol::<fproc::LauncherMarker>()
                .context("failed to connect to external launcher service")?
        };
        Ok(proxy)
    }
}
