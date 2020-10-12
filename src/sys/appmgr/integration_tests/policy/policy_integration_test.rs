// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fidl_examples_echo as fidl_echo,
    fuchsia_async::{self as fasync},
    fuchsia_component::client::{launch, launcher},
    lazy_static::lazy_static,
};

macro_rules! policy_url {
    ($cmx:expr) => {
        format!("{}{}", "fuchsia-pkg://fuchsia.com/policy-integration-tests#meta/", $cmx)
    };
}

lazy_static! {
    static ref NONE_ACCEPTED_URL: String = policy_url!("none.cmx");
    static ref PACKAGE_CACHE_DENIED_URL: String = policy_url!("package_cache_denied.cmx");
    static ref PACKAGE_CACHE_ALLOWED_URL: String = policy_url!("package_cache_allowed.cmx");
    static ref PACKAGE_RESOLVER_DENIED_URL: String = policy_url!("package_resolver_denied.cmx");
    static ref PACKAGE_RESOLVER_ALLOWED_URL: String = policy_url!("package_resolver_allowed.cmx");
    static ref ROOT_JOB_DENIED_URL: String = policy_url!("root_job_denied.cmx");
    static ref ROOT_JOB_ALLOWED_URL: String = policy_url!("root_job_allowed.cmx");
    static ref MMIO_RESOURCE_DENIED_URL: String = policy_url!("mmio_resource_denied.cmx");
    static ref MMIO_RESOURCE_ALLOWED_URL: String = policy_url!("mmio_resource_allowed.cmx");
    static ref ROOT_RESOURCE_DENIED_URL: String = policy_url!("root_resource_denied.cmx");
    static ref ROOT_RESOURCE_ALLOWED_URL: String = policy_url!("root_resource_allowed.cmx");
    static ref VMEX_RESOURCE_DENIED_URL: String = policy_url!("vmex_resource_denied.cmx");
    static ref VMEX_RESOURCE_ALLOWED_URL: String = policy_url!("vmex_resource_allowed.cmx");
    static ref PKGFS_VERSIONS_DENIED_URL: String = policy_url!("pkgfs_versions_denied.cmx");
    static ref PKGFS_VERSIONS_ALLOWED_URL: String = policy_url!("pkgfs_versions_allowed.cmx");
    static ref DEPRECATED_SHELL_DENIED_URL: String = policy_url!("deprecated_shell_denied.cmx");
    static ref DEPRECATED_SHELL_ALLOWED_URL: String = policy_url!("deprecated_shell_allowed.cmx");
    static ref DEPRECATED_EXEC_DENIED_URL: String =
        policy_url!("deprecated_ambient_replace_as_exec_denied.cmx");
    static ref DEPRECATED_EXEC_ALLOWED_URL: String =
        policy_url!("deprecated_ambient_replace_as_exec_allowed.cmx");
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

#[fasync::run_singlethreaded(test)]
async fn package_resolver_allowed() -> Result<(), Error> {
    assert_launch_allowed(&PACKAGE_RESOLVER_ALLOWED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn package_resolver_denied() -> Result<(), Error> {
    assert_launch_denied(&PACKAGE_RESOLVER_DENIED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn root_job_allowed() -> Result<(), Error> {
    assert_launch_allowed(&ROOT_JOB_ALLOWED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn root_job_denied() -> Result<(), Error> {
    assert_launch_denied(&ROOT_JOB_DENIED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn mmio_resource_allowed() -> Result<(), Error> {
    assert_launch_allowed(&MMIO_RESOURCE_ALLOWED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn mmio_resource_denied() -> Result<(), Error> {
    assert_launch_denied(&MMIO_RESOURCE_DENIED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn root_resource_allowed() -> Result<(), Error> {
    assert_launch_allowed(&ROOT_RESOURCE_ALLOWED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn root_resource_denied() -> Result<(), Error> {
    assert_launch_denied(&ROOT_RESOURCE_DENIED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn vmex_resource_allowed() -> Result<(), Error> {
    assert_launch_allowed(&VMEX_RESOURCE_ALLOWED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn vmex_resource_denied() -> Result<(), Error> {
    assert_launch_denied(&VMEX_RESOURCE_DENIED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn pkgfs_versions_allowed() -> Result<(), Error> {
    assert_launch_allowed(&PKGFS_VERSIONS_ALLOWED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn pkgfs_versions_denied() -> Result<(), Error> {
    assert_launch_denied(&PKGFS_VERSIONS_DENIED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn deprecated_shell_allowed() -> Result<(), Error> {
    assert_launch_allowed(&DEPRECATED_SHELL_ALLOWED_URL).await;
    Ok(())
}

#[fasync::run_singlethreaded(test)]
async fn deprecated_shell_denied() -> Result<(), Error> {
    assert_launch_denied(&DEPRECATED_SHELL_DENIED_URL).await;
    Ok(())
}
