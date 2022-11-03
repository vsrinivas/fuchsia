// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common_utils::common::macros::{fx_err_and_bail, with_line};
use crate::component::types::{
    ComponentLaunchRequest, ComponentLaunchResponse, ComponentSearchRequest, ComponentSearchResult,
};
use anyhow::{format_err, Context as _, Error};
use component_debug::{
    list::{get_all_instances, ListFilter},
    show::find_instances,
};
use component_events::{events::*, matcher::*};
use fidl_fuchsia_component as fcomponent;
use fidl_fuchsia_component_decl as fcdecl;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_sys2 as fsys;
use fuchsia_component::client;
use serde_json::{from_value, Value};
use tracing::info;

// CFv2 components will be launched in the collection with this name.
static LAUNCHED_COMPONENTS_COLLECTION_NAME: &'static str = "launched_components";

/// Perform operations related to Component.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct ComponentFacade {}

impl ComponentFacade {
    pub fn new() -> ComponentFacade {
        ComponentFacade {}
    }

    /// Launch component with url and optional arguments and detach directly
    /// # Arguments
    /// * `args`: will be parsed to ComponentLaunchRequest in create_launch_app
    ///   with fields:
    ///   - `url`: url of the component (ending in `.cmx` for CFv1 or `.cm` for
    ///     CFv2)
    ///   - `arguments`: optional arguments for the component (CFv1 only)
    pub async fn launch(&self, args: Value) -> Result<ComponentLaunchResponse, Error> {
        let tag = "ComponentFacade::create_launch_app";
        let req: ComponentLaunchRequest = from_value(args)?;
        // check if it's full url
        let component_url = match req.url {
            Some(x) => {
                if !x.starts_with("fuchsia-pkg") {
                    return Err(format_err!("Need full component url to launch"));
                }
                info!(
                    "Executing Launch {} in Component Facade with arguments {:?}.",
                    x, req.arguments
                );
                x
            }
            None => return Err(format_err!("Need full component url to launch")),
        };
        if component_url.ends_with(".cmx") {
            return Err(format_err!("CFv1 components are no longer supported"));
        } else {
            if req.arguments.is_some() {
                return Err(format_err!(
                    "CFv2 components currently don't support command line arguments"
                ));
            }
            self.launch_v2(tag, &component_url).await
        }
    }

    /// Launch component with url and optional arguments and detach directly
    /// # Arguments
    /// * `tag`: the sl4f command tag/name
    /// * `url`: url of the component
    /// * `arguments`: optional arguments for the component
    async fn launch_v2(&self, tag: &str, url: &str) -> Result<ComponentLaunchResponse, Error> {
        let collection_name = LAUNCHED_COMPONENTS_COLLECTION_NAME;
        let child_name =
            if let (Some(last_dot), Some(last_slash)) = (url.rfind('.'), (url.rfind('/'))) {
                &url[last_slash + 1..last_dot]
            } else {
                fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Component URL must end with a manifest file name: {url}")
                )
            };

        // Subscribe to stopped events for child components and then
        // wait for the component's `Stopped` event, and exit this command.
        let mut event_stream = EventStream::open().await.unwrap();
        let realm = client::connect_to_protocol::<fcomponent::RealmMarker>()?;
        let mut collection_ref = fcdecl::CollectionRef { name: collection_name.to_string() };
        let child_decl = fcdecl::Child {
            name: Some(child_name.to_string()),
            url: Some(url.to_string()),
            startup: Some(fcdecl::StartupMode::Lazy), // Dynamic children can only be started lazily.
            environment: None,
            ..fcdecl::Child::EMPTY
        };
        let child_args = fcomponent::CreateChildArgs {
            numbered_handles: None,
            ..fcomponent::CreateChildArgs::EMPTY
        };
        if let Err(err) = realm.create_child(&mut collection_ref, child_decl, child_args).await? {
            fx_err_and_bail!(&with_line!(tag), format_err!("Failed to create CFv2 child: {err:?}"));
        }

        let mut child_ref = fcdecl::ChildRef {
            name: child_name.to_string(),
            collection: Some(collection_name.to_string()),
        };

        let (exposed_dir, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()?;
        if let Err(err) = realm.open_exposed_dir(&mut child_ref, server_end).await? {
            fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to open exposed directory for CFv2 child: {err:?}")
            );
        }

        // Connect to the Binder protocol to start the component.
        let _ = client::connect_to_protocol_at_dir_root::<fcomponent::BinderMarker>(&exposed_dir)?;

        // Important! The `moniker_regex` must end with `$` to ensure the
        // `EventMatcher` does not observe stopped events of child components of
        // the launched component.
        info!("Waiting for Stopped events for child {child_name}");
        let stopped_event = EventMatcher::ok()
            .moniker_regex(format!("./{LAUNCHED_COMPONENTS_COLLECTION_NAME}:{child_name}$"))
            .wait::<Stopped>(&mut event_stream)
            .await
            .context(format!("failed to observe {child_name} Stopped event"))?;

        if let Err(err) = realm.destroy_child(&mut child_ref).await? {
            fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to destroy CFv2 child: {err:?}")
            );
        }

        let stopped_payload =
            stopped_event.result().map_err(|err| anyhow!("StoppedError: {err:?}"))?;
        info!("Returning {stopped_payload:?} event for child {child_name}");
        match stopped_payload.status {
            ExitStatus::Crash(status) => {
                info!("Component terminated unexpectedly. Status: {status}");
                Ok(ComponentLaunchResponse::Fail(status as i64))
            }
            ExitStatus::Clean => Ok(ComponentLaunchResponse::Success),
        }
    }

    /// Search component with component's name under appmgr
    /// # Arguments
    /// * `args`: will be parsed to ComponentSearchRequest
    /// * `name`: name of the component (should be like "component.cmx")
    pub async fn search(&self, args: Value) -> Result<ComponentSearchResult, Error> {
        let tag = "ComponentFacade::search";
        let req: ComponentSearchRequest = from_value(args)?;
        let name = match req.name {
            Some(x) => {
                info!("Searching Component {} in ComponentSearch Facade", x,);
                x
            }
            None => return Err(format_err!("Need name of the component to search.")),
        };
        let query = client::connect_to_protocol::<fsys::RealmQueryMarker>()?;
        let explorer = client::connect_to_protocol::<fsys::RealmExplorerMarker>()?;
        let instances = match find_instances(name.to_string(), &explorer, &query).await {
            Ok(p) => p,
            Err(err) => fx_err_and_bail!(
                &with_line!(tag),
                format_err!("Failed to find component: {}, err: {:}", name.to_string(), err)
            ),
        };
        if instances.is_empty() {
            return Ok(ComponentSearchResult::NotFound);
        }
        Ok(ComponentSearchResult::Success)
    }

    /// List running components, returns a vector containing component full URL.
    pub async fn list(&self) -> Result<Vec<String>, Error> {
        info!("List running Component under appmgr in ComponentSearch Facade",);
        let query = client::connect_to_protocol::<fsys::RealmQueryMarker>()?;
        let explorer = client::connect_to_protocol::<fsys::RealmExplorerMarker>()?;
        let instances = get_all_instances(&explorer, &query, Some(ListFilter::Running)).await?;
        let urls: Vec<String> = instances.into_iter().filter_map(|i| i.url).collect();
        Ok(urls)
    }
}
