// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error, Result},
    errors::ffx_bail,
    fidl::endpoints::{ProtocolMarker, Proxy},
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_url::pkg_url::PkgUrl,
    moniker::{ChildMonikerBase, RelativeMoniker, RelativeMonikerBase},
    std::path::PathBuf,
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

pub const COMPONENT_BIND_HELP: &str = "Moniker format: {name}/{name}
Example: 'core/brightness_manager'";

pub const COMPONENT_STOP_HELP: &str = "Moniker format: {name}/{name}
Example: 'core/brightness_manager'";

/// Parse the given string as an relative moniker. The string should be a '/' delimited series
/// of child monikers without any instance identifiers, e.g. "/name1/name2"
pub fn parse_moniker(moniker: &str) -> Result<RelativeMoniker, Error> {
    let mut formatted_moniker = String::from("./");
    formatted_moniker.push_str(&moniker);

    let formatted_moniker = RelativeMoniker::parse_string_without_instances(&formatted_moniker)?;

    // TODO(fxbug.dev/77451): remove the unsupported error once LifecycleController supports instanceless monikers.
    if formatted_moniker.up_path().len() > 0 {
        ffx_bail!("monikers with non-empty up_path are not supported")
    }
    for child_moniker in formatted_moniker.down_path() {
        if child_moniker.collection().is_some() {
            ffx_bail!("monikers for instances in collections are not supported.
For more information about collections: https://fuchsia.dev/fuchsia-src/concepts/components/v2/realms?hl=en#collections.")
        }
    }

    Ok(formatted_moniker)
}

/// Return the lifecycle controller proxy to allow component tools to resolve, bind,
/// stop component manifests.
pub async fn get_lifecycle_controller_proxy(
    root: fio::DirectoryProxy,
) -> Result<fsys::LifecycleControllerProxy, Error> {
    let lifecycle_controller_path =
        PathBuf::from("debug").join(fsys::LifecycleControllerMarker::NAME);
    match io_util::open_node(
        &root,
        lifecycle_controller_path.as_path(),
        fio::OPEN_RIGHT_READABLE,
        0,
    ) {
        Ok(node_proxy) => Ok(fsys::LifecycleControllerProxy::from_channel(
            node_proxy.into_channel().expect("could not get channel from proxy"),
        )),
        Err(e) => ffx_bail!("could not open node proxy: {}", e),
    }
}

/// Verifies that `url` can be parsed as a fuchsia-pkg CM URL
/// Returns the name of the component manifest, if the parsing was successful.
pub fn verify_fuchsia_pkg_cm_url(url: &str) -> Result<String> {
    let url = match PkgUrl::parse(url) {
        Ok(url) => url,
        Err(e) => {
            return Err(anyhow!("URL parsing error: {:?}", e));
        }
    };

    let resource = url.resource().ok_or(anyhow!("URL does not contain a path to a manifest"))?;
    let manifest = resource
        .split('/')
        .last()
        .ok_or(anyhow!("Could not extract manifest filename from URL"))?;

    if let Some(name) = manifest.strip_suffix(".cm") {
        Ok(name.to_string())
    } else if manifest.ends_with(".cmx") {
        Err(anyhow!(
            "{} is a legacy component manifest. Run it using `ffx component run-legacy`",
            manifest
        ))
    } else {
        Err(anyhow!(
            "{} is not a component manifest! Component manifests must end in the `cm` extension.",
            manifest
        ))
    }
}

/// Create a v2 component instance with the given `name` and `url` under the given `collection`
/// using the given `realm_proxy`.
pub async fn create_component_instance(
    realm_proxy: &fsys::RealmProxy,
    name: String,
    url: String,
    collection: String,
) -> Result<()> {
    let mut collection = fsys::CollectionRef { name: collection };
    let decl = fsys::ChildDecl {
        name: Some(name),
        url: Some(url),
        startup: Some(fsys::StartupMode::Lazy),
        environment: None,
        ..fsys::ChildDecl::EMPTY
    };

    realm_proxy
        .create_child(&mut collection, decl, fsys::CreateChildArgs::EMPTY)
        .await?
        .map_err(|e| anyhow!("Error creating child: {:?}", e))
}
