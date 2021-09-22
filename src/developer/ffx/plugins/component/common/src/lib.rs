// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::{ffx_bail, ffx_error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_zircon_status::Status,
};

pub const SELECTOR_FORMAT_HELP: &str =
    "Selector format: <component moniker>:(in|out|exposed)[:<service name>].
Wildcards may be used anywhere in the selector.

Example: 'remote-control:out:*' would return all services in 'out' for
the component remote-control.

Note that moniker wildcards are not recursive: 'a/*/c' will only match
components named 'c' running in some sub-realm directly below 'a', and
no further.";

pub const COMPONENT_LIST_HELP: &str = "only format: 'cmx' / 'cml' / 'running' / 'stopped'.
Default option is displaying all components if no argument is entered.";

pub const COMPONENT_SHOW_HELP: &str = "Filter accepts a partial component name or url.

Example:
'appmgr', 'appmgr.cm', 'fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm'
will all return information about the appmgr component.";

pub async fn connect_to_lifecycle_controller(
    rcs_proxy: &rc::RemoteControlProxy,
) -> Result<fsys::LifecycleControllerProxy> {
    let (hub, server_end) = create_proxy::<fio::DirectoryMarker>()?;
    rcs_proxy
        .open_hub(server_end)
        .await?
        .map_err(|i| ffx_error!("Could not open hub: {}", Status::from_raw(i)))?;
    let (lifecycle_controller, server_end) = create_proxy::<fsys::LifecycleControllerMarker>()?;
    let server_end = server_end.into_channel();
    hub.open(
        fio::OPEN_RIGHT_WRITABLE | fio::OPEN_RIGHT_READABLE,
        fio::MODE_TYPE_SERVICE,
        "debug/fuchsia.sys2.LifecycleController",
        server_end.into(),
    )?;
    Ok(lifecycle_controller)
}

/// Verifies that `url` can be parsed as a fuchsia-pkg CM URL
/// Returns the name of the component manifest, if the parsing was successful.
pub fn verify_fuchsia_pkg_cm_url(url: &str) -> Result<String> {
    let url = match PkgUrl::parse(url) {
        Ok(url) => url,
        Err(e) => ffx_bail!("URL parsing error: {:?}", e),
    };

    let resource = url.resource().ok_or(ffx_error!("URL does not contain a path to a manifest"))?;
    let manifest = resource
        .split('/')
        .last()
        .ok_or(ffx_error!("Could not extract manifest filename from URL"))?;

    if let Some(name) = manifest.strip_suffix(".cm") {
        Ok(name.to_string())
    } else if manifest.ends_with(".cmx") {
        ffx_bail!(
            "{} is a legacy component manifest. Run it using `ffx component run-legacy`",
            manifest
        )
    } else {
        ffx_bail!(
            "{} is not a component manifest! Component manifests must end in the `cm` extension.",
            manifest
        )
    }
}
