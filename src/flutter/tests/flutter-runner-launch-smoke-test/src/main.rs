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
        fuchsia_component_test::{
            Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
        },
        fuchsia_scenic as scenic, fuchsia_zircon as zx,
        futures::future::Either,
        tracing::info,
    };

    const FLUTTER_COMPONENT_PACKAGE: &str = "pingable-flutter-component";
    const FLUTTER_COMPONENT_DEBUG: &str = "pingable-flutter-component-debug-build-cfg";
    const FLUTTER_COMPONENT_PROFILE: &str = "pingable-flutter-component-profile-build-cfg";
    const FLUTTER_COMPONENT_RELEASE: &str = "pingable-flutter-component-release-build-cfg";

    const TEXT_MANAGER: &str = "text_manager";

    fn derive_component_url(package: &str, component_name: &str) -> String {
        format!("fuchsia-pkg://fuchsia.com/{}#meta/{}.cm", package, component_name)
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_launch_debug_build_cfg() -> Result<(), Error> {
        launch_and_ping_component(FLUTTER_COMPONENT_PACKAGE, FLUTTER_COMPONENT_DEBUG).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_launch_profile_build_cfg() -> Result<(), Error> {
        launch_and_ping_component(FLUTTER_COMPONENT_PACKAGE, FLUTTER_COMPONENT_PROFILE).await
    }

    #[fasync::run_singlethreaded(test)]
    async fn can_launch_release_build_cfg() -> Result<(), Error> {
        launch_and_ping_component(FLUTTER_COMPONENT_PACKAGE, FLUTTER_COMPONENT_RELEASE).await
    }

    async fn launch_and_connect_view_service(
        package: &str,
        component_name: &str,
    ) -> Result<RealmInstance, Error> {
        let component_url = &derive_component_url(package, component_name);
        info!("Attempting to launch {}", component_url);

        let builder = RealmBuilder::new_with_collection("flutter_runner_collection").await?;

        let text_manager = builder
            .add_child(
                TEXT_MANAGER,
                &derive_component_url(TEXT_MANAGER, TEXT_MANAGER),
                ChildOptions::new(),
            )
            .await?;

        // Add component to the realm, which is fetched using a URL.
        let flutter_component =
            builder.add_child(component_name, component_url, ChildOptions::new().eager()).await?;

        // Route capabilities from flutter components to test component.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.ui.app.ViewProvider"))
                    .capability(Capability::protocol_by_name("test.fuchsia.flutter.Pinger"))
                    .from(&flutter_component)
                    .to(Ref::parent()),
            )
            .await?;

        // Route capabilities from parent to flutter components.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.fonts.Provider"))
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .capability(Capability::protocol_by_name("fuchsia.ui.scenic.Scenic"))
                    .from(Ref::parent())
                    .to(&flutter_component),
            )
            .await?;

        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.ui.input.ImeService"))
                    .capability(Capability::protocol_by_name("fuchsia.ui.input3.Keyboard"))
                    .from(&text_manager)
                    .to(&flutter_component),
            )
            .await?;

        let realm = builder.build().await?;

        // We need to request a view from the component because the flutter runner
        // does not start up the vm service until this happens.
        let view_provider = realm
            .root
            .connect_to_protocol_at_exposed_dir::<ViewProviderMarker>()
            .context("Failed to connect to view_provider service")?;
        let token_pair = scenic::ViewTokenPair::new()?;
        let mut viewref_pair = scenic::ViewRefPair::new()?;

        view_provider.create_view_with_view_ref(
            token_pair.view_token.value,
            &mut viewref_pair.control_ref,
            &mut viewref_pair.view_ref,
        )?;

        Ok(realm)
    }

    async fn launch_and_ping_component(package: &str, component_name: &str) -> Result<(), Error> {
        // To ensure that we have launched and can receive messages we connect
        // to the ping service to try to communicate with the component.
        let realm = launch_and_connect_view_service(package, component_name).await?;

        let pinger = realm
            .root
            .connect_to_protocol_at_exposed_dir::<fidl_ping::PingerMarker>()
            .expect("connect to Realm service");

        check_for_ping_response(&pinger).await?;

        realm.destroy().await.context("destroy realm")?;

        Ok(())
    }

    /// Calls the ping server until a response is received.
    async fn check_for_ping_response(pinger: &fidl_ping::PingerProxy) -> Result<(), Error> {
        const MAX_ATTEMPTS: usize = 10;
        let timeout = zx::Duration::from_millis(2000);

        // Test multiple times in case the component is slow launching.
        for attempt in 1..=MAX_ATTEMPTS {
            info!("Calling pinger server, attempt: {}", attempt);

            let timeout_fut = fasync::Timer::new(timeout.after_now());
            let either = futures::future::select(timeout_fut, pinger.ping());
            let resolved = either.await;

            match resolved {
                Either::Left(_) => {
                    info!("Timeout calling pinger server, attempt: {}", attempt);
                }
                Either::Right((_, _)) => {
                    info!("Pingable flutter component launched successfully!");
                    return Ok(());
                }
            }
        }
        Err(format_err!("Failed to connect to pinger service"))
    }
}
