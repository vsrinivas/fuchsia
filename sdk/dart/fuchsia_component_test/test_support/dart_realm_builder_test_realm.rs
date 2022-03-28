// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Context as _, Error},
    cm_rust,
    component_events::events::Event,
    component_events::{events::*, matcher::*},
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_intl as fintl, fidl_fuchsia_sys as fsys,
    fidl_fuchsia_sys2 as fsys2, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::new::{
        Capability, ChildOptions, DirectoryContents, LocalComponentHandles, RealmBuilder, Ref,
        Route,
    },
    futures::prelude::*,
};

const DART_RUNNERS_ENVIRONMENT_NAME: &'static str = "dart_runners_env";

const TEST_NAME: &str = "fuchsia-component-test-dart-tests";

/// Wraps the dart realm builder test component, to launch and run as a Fuchsia
/// component test.
#[fuchsia::test]
async fn fuchsia_component_test_dart_tests() -> Result<(), Error> {
    let builder = RealmBuilder::new().await?;

    configure_dart_runner_environment(&builder, DART_RUNNERS_ENVIRONMENT_NAME).await?;

    let test_url = format!("#meta/{}.cm", TEST_NAME);

    // Add dart_component_to_test to the realm.
    let dart_component_to_test = builder
        .add_child(
            TEST_NAME,
            &test_url,
            ChildOptions::new().eager().environment(DART_RUNNERS_ENVIRONMENT_NAME),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fidl_fuchsia_logger::LogSinkMarker>())
                .capability(Capability::protocol::<fsys::EnvironmentMarker>())
                .capability(Capability::protocol::<fsys::LauncherMarker>())
                .capability(Capability::protocol::<fsys::LoaderMarker>())
                .capability(Capability::protocol::<fsys2::EventSourceMarker>())
                .capability(Capability::storage("data"))
                .from(Ref::parent())
                .to(&dart_component_to_test),
        )
        .await?;

    let _realm_instance = builder.build().await?;

    // Subscribe to stopped events for child components and then
    // wait for dart_component_to_test's `Stopped` event, and exit this test.
    let event_source = EventSource::new().unwrap();
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![Stopped::NAME])])
        .await
        .context("failed to subscribe to EventSource")?;

    // Important! The `moniker_regex` must end with `$` to ensure the
    // `EventMatcher` does not observe stopped events of child components of
    // the Dart realm builder tests.
    let event = EventMatcher::ok()
        .moniker_regex(format!("./{}$", TEST_NAME))
        .wait::<Stopped>(&mut event_stream)
        .await
        .context(format!("failed to observe {} Stopped event", TEST_NAME))?;

    let stopped_payload = event.result().map_err(|e| anyhow!("StoppedError: {:?}", e))?;

    if let ExitStatus::Crash(status) = stopped_payload.status {
        return Err(anyhow!(
            "Test failed with exit status: {} from component event: {:?}",
            status,
            event
        ));
        // Note that just printing an `error!()` log can _sometimes_ fail the
        // test, but sadly this is not reliable (timing issue?), so we must
        // return `Err()`--which (also, sadly) spews out a long and irrelevant
        // stacktrace from the Rust test. (I know it traces back to here, but
        // I'm debugging the Dart test, not the Rust test harness.)
    }
    Ok(())
}

async fn configure_dart_runner_environment(
    builder: &RealmBuilder,
    environment_name: &str,
) -> Result<(), Error> {
    let dart_runner_name =
        if cfg!(use_dart_aot_runner) { "dart_aot_runner" } else { "dart_jit_runner" };

    let dart_runner_url =
        format!("fuchsia-pkg://fuchsia.com/{}#meta/{}.cm", dart_runner_name, dart_runner_name);

    // Add the Dart runner child component, and route directories and services
    // the runner requires.
    let dart_runner =
        builder.add_child(dart_runner_name, dart_runner_url, ChildOptions::new()).await?;

    builder
        .read_only_directory("config-data", vec![&dart_runner], DirectoryContents::new())
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fidl_fuchsia_logger::LogSinkMarker>())
                .capability(Capability::protocol::<fidl_fuchsia_posix_socket::ProviderMarker>())
                .capability(Capability::protocol::<fidl_fuchsia_tracing_provider::RegistryMarker>())
                .from(Ref::parent())
                .to(&dart_runner),
        )
        .await?;

    let local_intl_property_provider = builder
        .add_local_child(
            "local_intl_property_provider",
            move |handles| Box::pin(local_intl_property_provider_impl(handles)),
            ChildOptions::new(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fintl::PropertyProviderMarker>())
                .from(&local_intl_property_provider)
                .to(&dart_runner),
        )
        .await?;

    // Add a placeholder component and routes for capabilities that are not
    // expected to be used in this test scenario.
    let placeholder = builder
        .add_local_child(
            "placeholder",
            |_: LocalComponentHandles| futures::future::pending().boxed(),
            ChildOptions::new(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<fidl_fuchsia_feedback::CrashReporterMarker>())
                .from(&placeholder)
                .to(&dart_runner),
        )
        .await?;

    // Add the runner to the environment the child will be launched in.
    let mut realm_decl = builder.get_realm_decl().await?;
    realm_decl.environments.push(cm_rust::EnvironmentDecl {
        name: String::from(environment_name),
        extends: fdecl::EnvironmentExtends::Realm,
        resolvers: vec![],
        runners: vec![cm_rust::RunnerRegistration {
            source_name: dart_runner_name.into(),
            source: cm_rust::RegistrationSource::Child(dart_runner_name.to_string()),
            target_name: dart_runner_name.into(),
        }],
        debug_capabilities: vec![],
        stop_timeout_ms: None,
    });
    builder.replace_realm_decl(realm_decl).await?;

    Ok(())
}

async fn local_intl_property_provider_impl(handles: LocalComponentHandles) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |mut stream: fintl::PropertyProviderRequestStream| {
        fasync::Task::local(async move {
            while let Some(fintl::PropertyProviderRequest::GetProfile { responder }) =
                stream.try_next().await.unwrap()
            {
                responder
                    .send(fintl::Profile {
                        time_zones: Some(vec![fintl::TimeZoneId {
                            id: "America/Los_Angeles".to_string(),
                        }]),
                        ..fintl::Profile::EMPTY
                    })
                    .expect("Error sending fuchsia::intl::PropertyProvider profile response");
            }
        })
        .detach();
    });
    fs.serve_connection(handles.outgoing_dir.into_channel()).unwrap();
    fs.collect::<()>().await;

    Ok(())
}
