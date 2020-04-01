// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fidl_examples_echo as fidl_echo,
    fuchsia_async::{self as fasync},
    fuchsia_component::client::{launch, launcher},
    lazy_static::lazy_static,
};

lazy_static! {
    static ref NONE_ACCEPTED_URL: String =
        "fuchsia-pkg://fuchsia.com/policy-integration-tests#meta/none.cmx".to_string();
    static ref PACKAGE_CACHE_DENIED_URL: String =
        "fuchsia-pkg://fuchsia.com/policy-integration-tests#meta/package_cache_denied.cmx"
            .to_string();
    static ref PACKAGE_CACHE_ALLOWED_URL: String =
        "fuchsia-pkg://fuchsia.com/policy-integration-tests#meta/package_cache_allowed.cmx"
            .to_string();
}

async fn launch_component(component_url: &str) -> Result<String, Error> {
    let launcher = launcher().context("failed to open the launcher")?;
    let app =
        launch(&launcher, component_url.to_string(), None).context("failed to launch service")?;
    let echo = app
        .connect_to_service::<fidl_echo::EchoMarker>()
        .context("Failed to connect to echo service")?;
    let result = echo.echo_string(Some("policy")).await?;
    Ok(result.unwrap())
}

async fn assert_launch_allowed(component_url: &str) {
    assert!(launch_component(component_url).await.unwrap() == "policy")
}

async fn assert_launch_denied(component_url: &str) {
    assert!(launch_component(component_url).await.is_err())
}

#[fasync::run_singlethreaded(test)]
async fn none_allowed() -> Result<(), Error> {
    assert_launch_allowed(&NONE_ACCEPTED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn package_cache_allowed() -> Result<(), Error> {
    assert_launch_allowed(&PACKAGE_CACHE_ALLOWED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn package_cache_denied() -> Result<(), Error> {
    assert_launch_denied(&PACKAGE_CACHE_DENIED_URL).await;
    Ok(())
}
