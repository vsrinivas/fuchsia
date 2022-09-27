// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{types::DiagnosticsProvider, Command, ListCommand},
        types::Error,
    },
    anyhow::anyhow,
    fidl as _, fidl_fuchsia_component_decl as fcomponent_decl, fidl_fuchsia_sys2 as fsys2,
    fuchsia_fs,
    futures::StreamExt,
    itertools::Itertools,
    lazy_static::lazy_static,
    regex::Regex,
    std::time::Duration,
};

lazy_static! {
    static ref EXPECTED_PROTOCOL_RE: &'static str = r".*fuchsia\.diagnostics\..*ArchiveAccessor$";
}

/// Returns the selectors for a component whose url contains the `manifest` string.
pub async fn get_selectors_for_manifest<P: DiagnosticsProvider>(
    manifest: &Option<String>,
    tree_selectors: &Vec<String>,
    accessor: &Option<String>,
    provider: &P,
) -> Result<Vec<String>, Error> {
    match &manifest {
        None => Ok(tree_selectors.clone()),
        Some(manifest) => {
            let list_command = ListCommand {
                manifest: Some(manifest.clone()),
                with_url: false,
                accessor: accessor.clone(),
            };
            let monikers = list_command
                .execute(provider)
                .await?
                .into_iter()
                .map(|item| item.into_moniker())
                .collect::<Vec<_>>();
            if monikers.is_empty() {
                Err(Error::ManifestNotFound(manifest.clone()))
            } else if tree_selectors.is_empty() {
                Ok(monikers.into_iter().map(|moniker| format!("{}:root", moniker)).collect())
            } else {
                Ok(monikers
                    .into_iter()
                    .flat_map(|moniker| {
                        tree_selectors
                            .iter()
                            .map(move |tree_selector| format!("{}:{}", moniker, tree_selector))
                    })
                    .collect())
            }
        }
    }
}

/// Expand selectors.
pub fn expand_selectors(selectors: Vec<String>) -> Result<Vec<String>, Error> {
    let mut result = vec![];
    for selector in selectors {
        match selectors::tokenize_string(&selector, selectors::SELECTOR_DELIMITER) {
            Ok(tokens) => {
                if tokens.len() > 1 {
                    result.push(selector);
                } else if tokens.len() == 1 {
                    result.push(format!("{}:*", selector));
                } else {
                    return Err(Error::InvalidArguments(format!(
                        "Iquery selectors cannot be empty strings: {:?}",
                        selector
                    )));
                }
            }
            Err(e) => {
                return Err(Error::InvalidArguments(format!(
                    "Tokenizing a provided selector failed. Error: {:?} Selector: {:?}",
                    e, selector
                )));
            }
        }
    }
    Ok(result)
}

/// Helper method to get all `InstanceInfo` from the `RealmExplorer`.
async fn get_instance_infos(
    explorer_proxy: &mut fsys2::RealmExplorerProxy,
) -> Result<Vec<fsys2::InstanceInfo>, Error> {
    // Server creates a client_end iterator for us.
    let infos = explorer_proxy
        .get_all_instance_infos()
        .await
        .map_err(|e| Error::ConnectingTo("RealmExplorer".to_owned(), e))?
        .map_err(|e| Error::CommunicatingWith("RealmExplorer".to_owned(), anyhow!("{:?}", e)))?
        .into_proxy()
        .map_err(|e| Error::ConnectingTo("InstanceInfoIterator".to_owned(), e))?;

    let mut output_vec = vec![];

    loop {
        let instance_infos = infos
            .next()
            .await
            .map_err(|e| Error::ConnectingTo("InstanceInfoIterator".to_owned(), e))?;

        if instance_infos.is_empty() {
            break;
        }
        output_vec.extend(instance_infos);
    }
    Ok(output_vec)
}

async fn strip_leading_relative_moniker(moniker: &str) -> &str {
    return if moniker.starts_with("./") { &moniker[2..] } else { moniker };
}

const READDIR_TIMEOUT_SECONDS: u64 = 5;

/// Get all the exposed `ArchiveAccessor` from any child component which
/// directly exposes them or places them in its outgoing directory.
pub async fn get_accessor_selectors(
    explorer_proxy: &mut fsys2::RealmExplorerProxy,
    query_proxy: &mut fsys2::RealmQueryProxy,
    paths: &[String],
) -> Result<Vec<String>, Error> {
    let instance_infos = get_instance_infos(explorer_proxy).await?;
    let expected_accessor_re = Regex::new(&EXPECTED_PROTOCOL_RE).unwrap();

    let mut output_vec = vec![];

    for ref instance_info in instance_infos {
        // Use the `RealmQuery `protocol to obtain detailed info for the instances.
        let (ret_instance_info, res_info) = match query_proxy
            .get_instance_info(&instance_info.moniker)
            .await
            .map_err(|e| Error::ConnectingTo("RealmQuery".to_owned(), e))?
            .map_err(|e| Error::CommunicatingWith("RealmQuery".to_owned(), anyhow!("{:?}", e)))
        {
            Ok(res) => res,
            Err(_) => continue,
        };

        let normalized_moniker = strip_leading_relative_moniker(&ret_instance_info.moniker).await;

        if let Some(resolved_state) = res_info {
            if !paths.is_empty() && !paths.iter().any(|path| normalized_moniker.starts_with(path)) {
                // We have a path parameter and the moniker is not matched.
                continue;
            }

            resolved_state.exposes.iter().for_each(|expose| {
                if let fcomponent_decl::Expose::Protocol(ref expose_protocol) = expose {
                    if let Some(ref expose_target) = expose_protocol.target_name {
                        // Only show directly exposed protocols.
                        if expected_accessor_re.is_match(expose_target)
                            && matches!(
                                expose_protocol.source,
                                Some(fcomponent_decl::Ref::Self_(_))
                            )
                        {
                            // Push "<relative_moniker>:expose:<service_name>" into the output vector.
                            // Stripes out the leading "./".
                            output_vec
                                .push(format!("{}:expose:{}", normalized_moniker, expose_target));
                        }
                    }
                }
            });
            // Look at `out` dir to find anything matches.
            let execution_state = match resolved_state.execution {
                Some(state) => state,
                _ => continue,
            };
            let out_dir = match execution_state.out_dir {
                Some(out_dir) => out_dir,
                _ => continue,
            };
            let out_dir_proxy = match out_dir.into_proxy() {
                Ok(proxy) => proxy,
                Err(_) => continue,
            };

            let entries = fuchsia_fs::directory::readdir_recursive(
                &out_dir_proxy,
                Some(Duration::from_secs(READDIR_TIMEOUT_SECONDS).into()),
            )
            .collect::<Vec<_>>()
            .await;

            for ref entry in entries {
                match entry {
                    Ok(ref dir_entry) => {
                        if (dir_entry.kind == fuchsia_fs::directory::DirentKind::File
                            || dir_entry.kind == fuchsia_fs::directory::DirentKind::Service)
                            && expected_accessor_re.is_match(&dir_entry.name)
                        {
                            // Split the entry so that field is a single element.
                            let dir_entry_elements = &dir_entry.name.split("/").collect::<Vec<_>>();
                            let (service_prefix, service) = if dir_entry_elements.len() > 1 {
                                (
                                    ["out"]
                                        .iter()
                                        .chain(
                                            dir_entry_elements[0..dir_entry_elements.len() - 1]
                                                .into_iter(),
                                        )
                                        .join("/"),
                                    dir_entry_elements.last().unwrap(),
                                )
                            } else {
                                ("out".to_owned(), dir_entry_elements.first().unwrap())
                            };

                            output_vec.push(format!(
                                "{}:{}:{}",
                                normalized_moniker, service_prefix, service
                            ));
                        }
                    }
                    Err(fuchsia_fs::directory::Error::Timeout) => {
                        eprintln!(
                            "Warning: Read directory timed out after {} second(s).",
                            READDIR_TIMEOUT_SECONDS
                        );
                    }
                    Err(e) => {
                        eprintln!("Warning: Unable to open directory {:?}.", e);
                    }
                }
            }
        }
    }
    Ok(output_vec)
}

#[cfg(test)]
mod test {
    use {
        super::*,
        assert_matches::assert_matches,
        iquery_test_support::{MockRealmExplorer, MockRealmQuery},
        std::sync::Arc,
    };

    #[fuchsia::test]
    async fn test_get_accessors_no_paths() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let fake_realm_explorer = Arc::new(MockRealmExplorer::default());
        let mut realm_query = Arc::clone(&fake_realm_query).get_proxy().await;
        let mut realm_explorer = Arc::clone(&fake_realm_explorer).get_proxy().await;

        let res = get_accessor_selectors(&mut realm_explorer, &mut realm_query, &vec![]).await;

        assert_matches!(res, Ok(_));

        assert_eq!(
            res.unwrap(),
            vec![
                String::from("example/component:expose:fuchsia.diagnostics.ArchiveAccessor"),
                String::from("other/component:out:fuchsia.diagnostics.MagicArchiveAccessor"),
                String::from("foo/component:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
                String::from("foo/bar/thing:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
            ]
        );
    }

    #[fuchsia::test]
    async fn test_get_accessors_valid_path() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let fake_realm_explorer = Arc::new(MockRealmExplorer::default());
        let mut realm_query = Arc::clone(&fake_realm_query).get_proxy().await;
        let mut realm_explorer = Arc::clone(&fake_realm_explorer).get_proxy().await;

        let res = get_accessor_selectors(
            &mut realm_explorer,
            &mut realm_query,
            &vec!["example".to_owned()],
        )
        .await;

        assert_matches!(res, Ok(_));

        assert_eq!(
            res.unwrap(),
            vec![String::from("example/component:expose:fuchsia.diagnostics.ArchiveAccessor"),]
        );
    }

    #[fuchsia::test]
    async fn test_get_accessors_valid_path_with_slash() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let fake_realm_explorer = Arc::new(MockRealmExplorer::default());
        let mut realm_query = Arc::clone(&fake_realm_query).get_proxy().await;
        let mut realm_explorer = Arc::clone(&fake_realm_explorer).get_proxy().await;

        let res = get_accessor_selectors(
            &mut realm_explorer,
            &mut realm_query,
            &vec!["example/".to_owned()],
        )
        .await;

        assert_matches!(res, Ok(_));

        assert_eq!(
            res.unwrap(),
            vec![String::from("example/component:expose:fuchsia.diagnostics.ArchiveAccessor"),]
        );
    }

    #[fuchsia::test]
    async fn test_get_accessors_valid_path_deep() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let fake_realm_explorer = Arc::new(MockRealmExplorer::default());
        let mut realm_query = Arc::clone(&fake_realm_query).get_proxy().await;
        let mut realm_explorer = Arc::clone(&fake_realm_explorer).get_proxy().await;

        let res = get_accessor_selectors(
            &mut realm_explorer,
            &mut realm_query,
            &vec!["foo/bar/thing".to_owned()],
        )
        .await;

        assert_matches!(res, Ok(_));

        assert_eq!(
            res.unwrap(),
            vec![String::from("foo/bar/thing:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),]
        );
    }

    #[fuchsia::test]
    async fn test_get_accessors_multi_component() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let fake_realm_explorer = Arc::new(MockRealmExplorer::default());
        let mut realm_query = Arc::clone(&fake_realm_query).get_proxy().await;
        let mut realm_explorer = Arc::clone(&fake_realm_explorer).get_proxy().await;

        let res =
            get_accessor_selectors(&mut realm_explorer, &mut realm_query, &vec!["foo/".to_owned()])
                .await;

        assert_matches!(res, Ok(_));

        assert_eq!(
            res.unwrap(),
            vec![
                String::from("foo/component:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
                String::from("foo/bar/thing:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
            ]
        );
    }
    #[fuchsia::test]
    async fn test_get_accessors_multi_path() {
        let fake_realm_query = Arc::new(MockRealmQuery::default());
        let fake_realm_explorer = Arc::new(MockRealmExplorer::default());
        let mut realm_query = Arc::clone(&fake_realm_query).get_proxy().await;
        let mut realm_explorer = Arc::clone(&fake_realm_explorer).get_proxy().await;

        let res = get_accessor_selectors(
            &mut realm_explorer,
            &mut realm_query,
            &vec!["foo/".to_owned(), "example/".to_owned()],
        )
        .await;

        assert_matches!(res, Ok(_));

        assert_eq!(
            res.unwrap(),
            vec![
                String::from("example/component:expose:fuchsia.diagnostics.ArchiveAccessor"),
                String::from("foo/component:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
                String::from("foo/bar/thing:expose:fuchsia.diagnostics.FeedbackArchiveAccessor"),
            ]
        );
    }

    #[fuchsia::test]
    async fn test_strip_leading_relative_moniker() {
        assert_eq!(strip_leading_relative_moniker("./example/stuff").await, "example/stuff");
        assert_eq!(strip_leading_relative_moniker("/example/stuff").await, "/example/stuff");
        assert_eq!(strip_leading_relative_moniker("example/stuff").await, "example/stuff");
    }
}
