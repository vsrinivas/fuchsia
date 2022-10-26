// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::commands::{
        list_files::{list_files, ListFilesResultItem},
        types::DiagnosticsProvider,
        utils::*,
    },
    crate::types::Error,
    anyhow::anyhow,
    async_trait::async_trait,
    diagnostics_data::{Data, DiagnosticsData},
    diagnostics_reader::ArchiveReader,
    fidl_fuchsia_diagnostics::{
        ArchiveAccessorMarker, ArchiveAccessorProxy, Selector, StringSelector, TreeSelector,
    },
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys2,
    fuchsia_component::client,
    fuchsia_fs,
    lazy_static::lazy_static,
    selectors,
};

lazy_static! {
    static ref ROOT_REALM_EXPLORER: &'static str = "fuchsia.sys2.RealmExplorer.root";
    static ref ROOT_REALM_QUERY: &'static str = "fuchsia.sys2.RealmQuery.root";
    static ref ROOT_ARCHIVIST_ACCESSOR: &'static str =
        "./bootstrap/archivist:expose:fuchsia.diagnostics.ArchiveAccessor";
    static ref CURRENT_DIR: Vec<String> = vec![".".to_string()];
}

#[derive(Default)]
pub struct ArchiveAccessorProvider;

#[async_trait]
impl DiagnosticsProvider for ArchiveAccessorProvider {
    async fn snapshot<D>(
        &self,
        accessor: &Option<String>,
        selectors: &[String],
    ) -> Result<Vec<Data<D>>, Error>
    where
        D: DiagnosticsData,
    {
        let archive = connect_to_archivist_selector_str(accessor).await?;
        let selectors = selectors.iter().map(|s| s.as_ref());
        ArchiveReader::new()
            .with_archive(archive)
            .retry_if_empty(false)
            .add_selectors(selectors)
            .snapshot::<D>()
            .await
            .map_err(|e| Error::Fetch(e))
    }

    async fn get_accessor_paths(&self, paths: &Vec<String>) -> Result<Vec<String>, Error> {
        get_accessor_selectors_auto_proxy(paths).await
    }

    async fn list_files(&self, monikers: &[String]) -> Result<Vec<ListFilesResultItem>, Error> {
        list_files_auto_proxy(monikers).await
    }
}

async fn list_files_auto_proxy(monikers: &[String]) -> Result<Vec<ListFilesResultItem>, Error> {
    let (realm_query_proxy, realm_explorer_proxy) = connect_realm_protocols().await?;
    list_files(realm_query_proxy, realm_explorer_proxy, monikers).await
}

/// Helper method to connect to both the `RealmQuery` and the `RealmExplorer`.
pub(crate) async fn connect_realm_protocols(
) -> Result<(fsys2::RealmQueryProxy, fsys2::RealmExplorerProxy), Error> {
    let dir_proxy = fuchsia_fs::directory::open_in_namespace(
        "/svc",
        fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE,
    )
    .map_err(|e| {
        Error::IOError(
            "unable to connect to DirectoryProxy for /svc".to_owned(),
            anyhow!("{:?}", e),
        )
    })?;

    let realm_query_proxy =
        client::connect_to_named_protocol_at_dir_root::<fsys2::RealmQueryMarker>(
            &dir_proxy,
            &ROOT_REALM_QUERY,
        )
        .map_err(|e| Error::IOError("unable to connect to root RealmQuery".to_owned(), e))?;
    let realm_explorer_proxy = client::connect_to_named_protocol_at_dir_root::<
        fsys2::RealmExplorerMarker,
    >(&dir_proxy, &ROOT_REALM_EXPLORER)
    .map_err(|e| Error::IOError("unable to connect to root RealmExplorer".to_owned(), e))?;

    Ok((realm_query_proxy, realm_explorer_proxy))
}

/// Lists all ArchiveAccessor files under the provided paths. If no paths are provided, it'll list
/// everything under the Root Realm.
async fn get_accessor_selectors_auto_proxy(paths: &[String]) -> Result<Vec<String>, Error> {
    let (mut realm_query_proxy, mut realm_explorer_proxy) = connect_realm_protocols().await?;
    get_accessor_selectors(&mut realm_explorer_proxy, &mut realm_query_proxy, paths).await
}

/// Connect to `fuchsia.sys2.*ArchivistAccessor` with the provided selector string.
/// The selector string should be in the form of "<relative_moniker>:<expose|out>:<service_name>".
/// If no selector string is provided, it will try to connect to
/// `./bootstrap/archivist:expose:fuchsia.sys2.ArchiveAccessor`.
pub async fn connect_to_archivist_selector_str(
    selector: &Option<String>,
) -> Result<ArchiveAccessorProxy, Error> {
    let (mut realm_query_proxy, _) = connect_realm_protocols().await?;
    match selector {
        Some(s) => {
            let selector =
                selectors::parse_selector::<selectors::VerboseError>(s).map_err(|e| {
                    Error::ParseSelector("unable to parse selector".to_owned(), anyhow!("{:?}", e))
                })?;
            connect_to_archivist(&selector, &mut realm_query_proxy).await
        }
        None => connect_to_the_first_archivist(&mut realm_query_proxy).await,
    }
}

pub async fn connect_to_archivist_selector(
    selector: &Option<Selector>,
) -> Result<ArchiveAccessorProxy, Error> {
    let (mut realm_query_proxy, _) = connect_realm_protocols().await?;
    match selector {
        Some(s) => connect_to_archivist(s, &mut realm_query_proxy).await,
        None => connect_to_the_first_archivist(&mut realm_query_proxy).await,
    }
}

/// Connect to `bootstrap/archivist:expose:fuchsia.diagnostics.ArchiveAccessor`.
///
/// This function takes a `RealmQueryProxy` and try to connect to the `ArchiveAccessor`,
/// via the expose directory.
async fn connect_to_the_first_archivist(
    query_proxy: &mut fsys2::RealmQueryProxy,
) -> Result<ArchiveAccessorProxy, Error> {
    let selector = selectors::parse_selector::<selectors::VerboseError>(&ROOT_ARCHIVIST_ACCESSOR)
        .map_err(|e| {
        Error::ParseSelector("unable to parse selector".to_owned(), anyhow!("{:?}", e))
    })?;
    connect_to_archivist(&selector, query_proxy).await
}

// Use the provided `Selector` and depending on the selector,
// opens either the `expose` or `out` directory and return the proxy to
// it.
async fn get_dir_proxy(
    selector: &Selector,
    proxy: &mut fsys2::RealmQueryProxy,
) -> Result<(DirectoryProxy, String), Error> {
    let component = selector
        .component_selector
        .as_ref()
        .ok_or_else(|| Error::InvalidSelector("no component selector".to_owned()))?;
    let tree_selector = selector
        .tree_selector
        .as_ref()
        .ok_or_else(|| Error::InvalidSelector("no tree selector".to_owned()))?;
    let property_selector = match tree_selector {
        TreeSelector::PropertySelector(selector) => selector,
        _ => {
            return Err(Error::InvalidSelector("no property selector".to_owned()));
        }
    };

    if property_selector.node_path.len() != 1 {
        return Err(Error::InvalidSelector("expect a single property selector".to_owned()));
    }

    let property_node_selector = match property_selector.node_path[0] {
        StringSelector::ExactMatch(ref item) => item.to_owned(),
        _ => {
            return Err(Error::InvalidSelector(
                "property selector is not an exact match selector".to_owned(),
            ));
        }
    };

    let target_property = match property_selector.target_properties {
        StringSelector::ExactMatch(ref target_property) => target_property,
        _ => {
            return Err(Error::InvalidSelector(
                "selector is not an exact match selector".to_owned(),
            ));
        }
    };

    let component_selector = component
        .moniker_segments
        .as_ref()
        .ok_or_else(|| Error::InvalidSelector("no component selector".to_owned()))?;
    let mut moniker_segments = vec![];
    for component_segment in component_selector {
        if let StringSelector::ExactMatch(ref pat) = component_segment {
            moniker_segments.push(pat.to_owned());
        } else {
            return Err(Error::InvalidSelector("bad segment".to_owned()));
        }
    }

    let mut full_moniker = moniker_segments.join("/");
    if !full_moniker.starts_with("./") {
        full_moniker = format!("./{}", full_moniker);
    }

    // Get the instance info for `expose` dir.
    let (_, resolved_state) = proxy
        .get_instance_info(&full_moniker)
        .await
        .map_err(|e| Error::ConnectingTo("RealmQuery".to_owned(), e))?
        .map_err(|e| Error::CommunicatingWith("RealmQuery".to_owned(), anyhow!("{:?}", e)))?;

    let resolved_state =
        resolved_state.ok_or_else(|| Error::InvalidSelector("no component selector".to_owned()))?;

    let directory_proxy = if property_node_selector == "expose" {
        resolved_state
            .exposed_dir
            .into_proxy()
            .map_err(|e| Error::ConnectingTo("ArchiveAccessor".to_owned(), e))?
    } else if property_node_selector == "out" {
        let execution_state = match resolved_state.execution {
            Some(state) => state,
            _ => return Err(Error::InvalidSelector("no execution state".to_owned())),
        };
        let out_dir = match execution_state.out_dir {
            Some(out_dir) => out_dir,
            _ => return Err(Error::InvalidSelector("no outdir".to_owned())),
        };
        out_dir.into_proxy().map_err(|e| Error::ConnectingTo("ArchiveAccessor".to_owned(), e))?
    } else {
        return Err(Error::InvalidSelector(format!(
            "directory {} is not valid. Must be one of out|expose.",
            &property_node_selector
        )));
    };

    Ok((directory_proxy, target_property.to_owned()))
}

/// Attempt to connect to the `fuchsia.diagnostics.*ArchiveAccessor` with the selector
/// specified.
pub async fn connect_to_archivist(
    selector: &Selector,
    proxy: &mut fsys2::RealmQueryProxy,
) -> Result<ArchiveAccessorProxy, Error> {
    let (directory_proxy, target_property) = get_dir_proxy(selector, proxy).await?;

    let proxy = client::connect_to_named_protocol_at_dir_root::<ArchiveAccessorMarker>(
        &directory_proxy,
        &target_property,
    )
    .map_err(|e| Error::ConnectToArchivist(anyhow!("{:?}", e)))?;

    Ok(proxy)
}

#[cfg(test)]
mod test {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_diagnostics::{
            ComponentSelector, PropertySelector, Selector, StringSelector, TreeSelector,
        },
        iquery_test_support::MockRealmQuery,
        std::sync::Arc,
    };

    #[fuchsia::test]
    async fn test_get_dir_proxy_selector_empty() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let selector =
            Selector { component_selector: None, tree_selector: None, ..Selector::EMPTY };
        let mut proxy = Arc::clone(&fake_realm_query).get_proxy().await;

        assert_matches!(get_dir_proxy(&selector, &mut proxy).await, Err(_));
    }

    #[fuchsia::test]
    async fn test_get_dir_proxy_selector_bad_property_selector() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let selector = Selector {
            component_selector: Some(ComponentSelector {
                moniker_segments: Some(vec![
                    StringSelector::ExactMatch("example".to_owned()),
                    StringSelector::ExactMatch("component".to_owned()),
                ]),
                ..ComponentSelector::EMPTY
            }),
            tree_selector: Some({
                TreeSelector::PropertySelector(PropertySelector {
                    node_path: vec![StringSelector::ExactMatch("invalid".to_owned())],
                    target_properties: StringSelector::ExactMatch(
                        "fuchsia.diagnostics.MagicArchiveAccessor".to_owned(),
                    ),
                })
            }),
            ..Selector::EMPTY
        };
        let mut proxy = Arc::clone(&fake_realm_query).get_proxy().await;

        assert_matches!(get_dir_proxy(&selector, &mut proxy).await, Err(_));
    }
    #[fuchsia::test]
    async fn test_get_dir_proxy_selector_bad_component() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let selector = Selector {
            component_selector: Some(ComponentSelector {
                moniker_segments: Some(vec![
                    StringSelector::ExactMatch("bad".to_owned()),
                    StringSelector::ExactMatch("component".to_owned()),
                ]),
                ..ComponentSelector::EMPTY
            }),
            tree_selector: Some({
                TreeSelector::PropertySelector(PropertySelector {
                    node_path: vec![StringSelector::ExactMatch("out".to_owned())],
                    target_properties: StringSelector::ExactMatch(
                        "fuchsia.diagnostics.MagicArchiveAccessor".to_owned(),
                    ),
                })
            }),
            ..Selector::EMPTY
        };
        let mut proxy = Arc::clone(&fake_realm_query).get_proxy().await;

        assert_matches!(get_dir_proxy(&selector, &mut proxy).await, Err(_));
    }

    #[fuchsia::test]
    async fn test_get_dir_proxy_ok() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let selector = Selector {
            component_selector: Some(ComponentSelector {
                moniker_segments: Some(vec![
                    StringSelector::ExactMatch("example".to_owned()),
                    StringSelector::ExactMatch("component".to_owned()),
                ]),
                ..ComponentSelector::EMPTY
            }),
            tree_selector: Some({
                TreeSelector::PropertySelector(PropertySelector {
                    node_path: vec![StringSelector::ExactMatch("out".to_owned())],
                    target_properties: StringSelector::ExactMatch(
                        "fuchsia.diagnostics.MagicArchiveAccessor".to_owned(),
                    ),
                })
            }),
            ..Selector::EMPTY
        };
        let mut proxy = Arc::clone(&fake_realm_query).get_proxy().await;

        assert_matches!(get_dir_proxy(&selector, &mut proxy).await, Ok(_));
    }

    #[fuchsia::test]
    async fn test_get_dir_proxy_ok_expose() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let selector = Selector {
            component_selector: Some(ComponentSelector {
                moniker_segments: Some(vec![
                    StringSelector::ExactMatch("example".to_owned()),
                    StringSelector::ExactMatch("component".to_owned()),
                ]),
                ..ComponentSelector::EMPTY
            }),
            tree_selector: Some({
                TreeSelector::PropertySelector(PropertySelector {
                    node_path: vec![StringSelector::ExactMatch("expose".to_owned())],
                    target_properties: StringSelector::ExactMatch(
                        "fuchsia.diagnostics.MagicArchiveAccessor".to_owned(),
                    ),
                })
            }),
            ..Selector::EMPTY
        };
        let mut proxy = Arc::clone(&fake_realm_query).get_proxy().await;

        assert_matches!(get_dir_proxy(&selector, &mut proxy).await, Ok(_));
    }
}
