// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_sys::{
        EnvironmentControllerEvent, EnvironmentControllerProxy, EnvironmentMarker,
        EnvironmentOptions, EnvironmentProxy, LauncherProxy,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::{connect_to_service, launch},
    fuchsia_inspect::{assert_inspect_tree, testing},
    futures::stream::StreamExt,
    lazy_static::lazy_static,
};

lazy_static! {
    static ref TEST_COMPONENT: String = "test_component.cmx".to_string();
    static ref TEST_COMPONENT_URL: String =
        "fuchsia-pkg://fuchsia.com/component_events_integration_tests#meta/test_component.cmx"
            .to_string();
    static ref DIAGNOSTICS_ARGUMENTS: Vec<String> = vec!["with-diagnostics".to_string()];
}

// Validates that the realm paths with which the observer queries do not include the current realm.
#[fasync::run_singlethreaded(test)]
async fn test_observer_integration() -> Result<(), Error> {
    let env = connect_to_service::<EnvironmentMarker>().expect("connect to current environment");
    let (env1, _env1_ctrl, launcher1) = create_nested_environment(&env, "env1").await?;
    let (_env2, _env2_ctrl, launcher2) = create_nested_environment(&env1, "env2").await?;

    let _app_1 =
        launch(&launcher1, TEST_COMPONENT_URL.to_string(), Some(DIAGNOSTICS_ARGUMENTS.clone()))?;
    let _app_2 =
        launch(&launcher2, TEST_COMPONENT_URL.to_string(), Some(DIAGNOSTICS_ARGUMENTS.clone()))?;

    // The observer must have received results with a correct relative realm path (not including
    // the test realm).
    check_nested(Some(vec!["env1".to_string()]), 1).await?;
    check_nested(Some(vec!["env1".to_string(), "env2".to_string()]), 1).await?;

    // The observer must not have received a result containing the test realm.
    check_nested(Some(vec!["*".to_string(), "env1".to_string()]), 0).await?;

    // If no component selector is set, all are returned.
    check_nested(None, 3).await?;

    Ok(())
}

async fn check_nested(
    realm_path: Option<Vec<String>>,
    expected_results: usize,
) -> Result<(), Error> {
    let mut fetcher = testing::InspectDataFetcher::new();
    if let Some(mut moniker) = realm_path {
        moniker.push(TEST_COMPONENT.clone());
        fetcher = fetcher.add_selector(testing::ComponentSelector::new(moniker));
    }
    let results = fetcher.get().await?.into_iter().collect::<Vec<_>>();
    assert_eq!(results.len(), expected_results);
    for result in results {
        assert_inspect_tree!(result, root: contains {
            "fuchsia.inspect.Health": contains {
                status: "OK",
            }
        });
    }
    Ok(())
}

async fn create_nested_environment(
    parent: &EnvironmentProxy,
    label: &str,
) -> Result<(EnvironmentProxy, EnvironmentControllerProxy, LauncherProxy), Error> {
    let (new_env, new_env_server_end) =
        fidl::endpoints::create_proxy::<EnvironmentMarker>().context("could not create proxy")?;
    let (controller, controller_server_end) =
        fidl::endpoints::create_proxy().context("could not create proxy")?;

    // Component manager will run with these environment options
    let mut env_options = EnvironmentOptions {
        inherit_parent_services: true,
        use_parent_runners: true,
        kill_on_oom: true,
        delete_storage_on_death: true,
    };

    parent
        .create_nested_environment(
            new_env_server_end,
            controller_server_end,
            label,
            None,
            &mut env_options,
        )
        .context("could not create isolated environment")?;

    let EnvironmentControllerEvent::OnCreated {} = controller
        .take_event_stream()
        .next()
        .await
        .unwrap()
        .expect("failed to get env created event");

    let (launcher, launcher_server_end) =
        fidl::endpoints::create_proxy().context("could not create proxy")?;
    new_env
        .get_launcher(launcher_server_end)
        .context("could not get isolated environment launcher")?;

    Ok((new_env, controller, launcher))
}
