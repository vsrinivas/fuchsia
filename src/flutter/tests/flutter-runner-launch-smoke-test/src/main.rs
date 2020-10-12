// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_async as fasync;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::{format_err, Context as _, Error},
        fidl_fuchsia_ui_app::ViewProviderMarker,
        fidl_test_fuchsia_flutter as fidl_ping,
        fuchsia_async::DurationExt,
        fuchsia_component::client,
        fuchsia_scenic as scenic,
        fuchsia_syslog::{self as syslog, macros::*},
        fuchsia_zircon as zx,
        futures::future::Either,
    };

    #[fasync::run_singlethreaded(test)]
    async fn can_launch_debug_build_cfg() -> Result<(), Error> {
        syslog::init_with_tags(&["can_launch_debug_build_cfg"]).context("Can't init logger")?;
        check_launching_component("fuchsia-pkg://fuchsia.com/pingable-flutter-component#meta/pingable-flutter-component-debug-build-cfg.cmx").await
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_launch_profile_build_cfg() -> Result<(), Error> {
        syslog::init_with_tags(&["can_launch_profile_build_cfg"]).context("Can't init logger")?;
        check_launching_component("fuchsia-pkg://fuchsia.com/pingable-flutter-component#meta/pingable-flutter-component-profile-build-cfg.cmx").await
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_launch_release_build_cfg() -> Result<(), Error> {
        syslog::init_with_tags(&["can_launch_release_build_cfg"]).context("Can't init logger")?;
        check_launching_component("fuchsia-pkg://fuchsia.com/pingable-flutter-component#meta/pingable-flutter-component-release-build-cfg.cmx").await
    }

    async fn check_launching_component(component_url: &str) -> Result<(), Error> {
        fx_log_info!("Attempting to launch {}", component_url);

        let launcher = client::launcher().context("Failed to get the launcher")?;

        let app = client::launch(&launcher, component_url.to_string(), None)
            .context("failed to launch the dart service under test")?;

        // We need to request a view from the component because the flutter runner
        // does not start up the vm service until this happens.
        let view_provider = app
            .connect_to_service::<ViewProviderMarker>()
            .context("Failed to connect to view_provider service")?;
        let token_pair = scenic::ViewTokenPair::new()?;
        let mut viewref_pair = scenic::ViewRefPair::new()?;

        view_provider.create_view_with_view_ref(
            token_pair.view_token.value,
            &mut viewref_pair.control_ref,
            &mut viewref_pair.view_ref,
        )?;

        // To ensure that we have launched and can receive messages we connect
        // to the ping service to try to communicate with the component.
        let pinger = app
            .connect_to_service::<fidl_ping::PingerMarker>()
            .context("Failed to connect to pinger service")?;

        check_for_ping_response(&pinger).await?;

        Ok(())
    }

    /// Calls the ping server until a response is received.
    async fn check_for_ping_response(pinger: &fidl_ping::PingerProxy) -> Result<(), Error> {
        const MAX_ATTEMPTS: usize = 10;
        let timeout = zx::Duration::from_millis(1000);

        // Test multiple times in case the component is slow launching.
        for attempt in 1..=MAX_ATTEMPTS {
            fx_log_info!("Calling pinger server, attempt: {}", attempt);

            let timeout_fut = fasync::Timer::new(timeout.after_now());
            let either = futures::future::select(timeout_fut, pinger.ping());
            let resolved = either.await;

            match resolved {
                Either::Left(_) => {
                    fx_log_info!("Timeout calling pinger server, attempt: {}", attempt);
                }
                Either::Right((_, _)) => return Ok(()),
            }
        }
        Err(format_err!("Failed to connect to pinger service"))
    }
}
