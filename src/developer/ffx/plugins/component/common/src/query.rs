// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    component_debug::list::get_all_cml_instances,
    errors::ffx_bail,
    fidl_fuchsia_sys2 as fsys,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
};

/// Retrieves a list of CML instance monikers that will match a given string query.
///
/// The string query can be a partial match on the following properties:
/// * component moniker
/// * component URL
/// * component instance ID
// TODO(https://fxbug.dev/114806): `ffx component show` should use this method to get
// component monikers when CMX support has been deprecated.
pub async fn get_cml_monikers_from_query(
    query: &str,
    explorer: &fsys::RealmExplorerProxy,
) -> Result<Vec<AbsoluteMoniker>> {
    // The query parses successfully as an absolute moniker.
    // Assume that the client is interested in a specific component.
    if let Ok(moniker) = AbsoluteMoniker::parse_str(&query) {
        return Ok(vec![moniker]);
    }

    let instances = get_all_cml_instances(explorer).await?;

    // Try and find instances that contain the query in any of the identifiers
    // (moniker, URL, instance ID).
    let mut monikers: Vec<AbsoluteMoniker> = instances
        .into_iter()
        .filter(|i| {
            let url_match = i.url.as_ref().map_or(false, |url| url.contains(&query));
            let moniker_match = i.moniker.to_string().contains(&query);
            let id_match = i.instance_id.as_ref().map_or(false, |id| id.contains(&query));
            url_match || moniker_match || id_match
        })
        .map(|i| i.moniker)
        .collect();

    // For stability guarantees, sort the moniker list
    monikers.sort();

    Ok(monikers)
}

/// Retrieves exactly one CML instance moniker that will match a given string query.
///
/// The string query can be a partial match on the following properties:
/// * component moniker
/// * component URL
/// * component instance ID
///
/// If more than one instance matches the query, an error is thrown.
/// If no instance matches the query, an error is thrown.
pub async fn get_cml_moniker_from_query(
    query: &str,
    realm_explorer: &fsys::RealmExplorerProxy,
) -> Result<AbsoluteMoniker> {
    // Get all instance monikers that match the query and ensure there is only one.
    let mut monikers = get_cml_monikers_from_query(&query, &realm_explorer).await?;
    if monikers.len() > 1 {
        let monikers: Vec<String> = monikers.into_iter().map(|m| m.to_string()).collect();
        let monikers = monikers.join("\n");
        ffx_bail!("The query {:?} matches more than one component instance:\n{}\n\nTo avoid ambiguity, use one of the above monikers instead.\n", query, monikers);
    }
    if monikers.is_empty() {
        ffx_bail!("No matching component instance found for query {:?}. Use `ffx component list` to find the correct component instance.\n", query);
    }
    let moniker = monikers.remove(0);
    Ok(moniker)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy_and_stream, create_request_stream, ClientEnd},
        futures::TryStreamExt,
    };

    fn setup_fake_realm_explorer() -> fsys::RealmExplorerProxy {
        let (realm_explorer, mut stream) =
            create_proxy_and_stream::<fsys::RealmExplorerMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let fsys::RealmExplorerRequest::GetAllInstanceInfos { responder } =
                stream.try_next().await.unwrap().unwrap();
            let client_end = setup_fake_instance_info_iterator();
            responder.send(&mut Ok(client_end)).unwrap();
        })
        .detach();
        realm_explorer
    }

    fn setup_fake_instance_info_iterator() -> ClientEnd<fsys::InstanceInfoIteratorMarker> {
        let (client_end, mut stream) =
            create_request_stream::<fsys::InstanceInfoIteratorMarker>().unwrap();
        fuchsia_async::Task::local(async move {
            let fsys::InstanceInfoIteratorRequest::Next { responder } =
                stream.try_next().await.unwrap().unwrap();
            responder
                .send(
                    &mut vec![
                        fsys::InstanceInfo {
                            moniker: "./core/foo".to_string(),
                            url: "#meta/1bar.cm".to_string(),
                            instance_id: Some("123456".to_string()),
                            state: fsys::InstanceState::Resolved,
                        },
                        fsys::InstanceInfo {
                            moniker: "./core/boo".to_string(),
                            url: "#meta/2bar.cm".to_string(),
                            instance_id: Some("456789".to_string()),
                            state: fsys::InstanceState::Resolved,
                        },
                    ]
                    .iter_mut(),
                )
                .unwrap();

            // Now send nothing to indicate the end
            let fsys::InstanceInfoIteratorRequest::Next { responder } =
                stream.try_next().await.unwrap().unwrap();
            responder.send(&mut vec![].iter_mut()).unwrap();
        })
        .detach();
        client_end
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_monikers_from_query_moniker_more_than_1() {
        let explorer = setup_fake_realm_explorer();
        let mut results = get_cml_monikers_from_query("core", &explorer).await.unwrap();
        assert_eq!(results.len(), 2);

        let result = results.remove(0);
        assert_eq!(result, AbsoluteMoniker::parse_str("/core/boo").unwrap());

        let result = results.remove(0);
        assert_eq!(result, AbsoluteMoniker::parse_str("/core/foo").unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_monikers_from_query_moniker_exactly_1() {
        let explorer = setup_fake_realm_explorer();
        let mut results = get_cml_monikers_from_query("foo", &explorer).await.unwrap();
        assert_eq!(results.len(), 1);

        let result = results.remove(0);
        assert_eq!(result, AbsoluteMoniker::parse_str("/core/foo").unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_monikers_from_query_url_more_than_1() {
        let explorer = setup_fake_realm_explorer();
        let mut results = get_cml_monikers_from_query("bar.cm", &explorer).await.unwrap();
        assert_eq!(results.len(), 2);

        let result = results.remove(0);
        assert_eq!(result, AbsoluteMoniker::parse_str("/core/boo").unwrap());

        let result = results.remove(0);
        assert_eq!(result, AbsoluteMoniker::parse_str("/core/foo").unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_monikers_from_query_url_exactly_1() {
        let explorer = setup_fake_realm_explorer();
        let mut results = get_cml_monikers_from_query("2bar.cm", &explorer).await.unwrap();
        assert_eq!(results.len(), 1);

        let result = results.remove(0);
        assert_eq!(result, AbsoluteMoniker::parse_str("/core/boo").unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_monikers_from_query_id_more_than_1() {
        let explorer = setup_fake_realm_explorer();
        let mut results = get_cml_monikers_from_query("456", &explorer).await.unwrap();
        assert_eq!(results.len(), 2);

        let result = results.remove(0);
        assert_eq!(result, AbsoluteMoniker::parse_str("/core/boo").unwrap());

        let result = results.remove(0);
        assert_eq!(result, AbsoluteMoniker::parse_str("/core/foo").unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_monikers_from_query_id_exactly_1() {
        let explorer = setup_fake_realm_explorer();
        let mut results = get_cml_monikers_from_query("123", &explorer).await.unwrap();
        assert_eq!(results.len(), 1);

        let result = results.remove(0);
        assert_eq!(result, AbsoluteMoniker::parse_str("/core/foo").unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_monikers_from_query_no_results() {
        let explorer = setup_fake_realm_explorer();
        let results = get_cml_monikers_from_query("qwerty", &explorer).await.unwrap();
        assert_eq!(results.len(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_monikers_from_query_parses_as_moniker() {
        let explorer = setup_fake_realm_explorer();
        let mut results = get_cml_monikers_from_query("/qwerty", &explorer).await.unwrap();
        assert_eq!(results.len(), 1);

        let result = results.remove(0);
        assert_eq!(result, AbsoluteMoniker::parse_str("/qwerty").unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_moniker_from_query_parses_as_moniker() {
        let explorer = setup_fake_realm_explorer();
        let moniker = get_cml_moniker_from_query("/qwerty", &explorer).await.unwrap();
        assert_eq!(moniker, AbsoluteMoniker::parse_str("/qwerty").unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_moniker_from_query_no_match() {
        let explorer = setup_fake_realm_explorer();
        get_cml_moniker_from_query("qwerty", &explorer).await.unwrap_err();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_moniker_from_query_multiple_match() {
        let explorer = setup_fake_realm_explorer();
        get_cml_moniker_from_query("bar.cm", &explorer).await.unwrap_err();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_moniker_from_query_moniker_single_match() {
        let explorer = setup_fake_realm_explorer();
        let moniker = get_cml_moniker_from_query("foo", &explorer).await.unwrap();
        assert_eq!(moniker, AbsoluteMoniker::parse_str("/core/foo").unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_moniker_from_url_moniker_single_match() {
        let explorer = setup_fake_realm_explorer();
        let moniker = get_cml_moniker_from_query("2bar.cm", &explorer).await.unwrap();
        assert_eq!(moniker, AbsoluteMoniker::parse_str("/core/boo").unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_cml_moniker_from_url_id_single_match() {
        let explorer = setup_fake_realm_explorer();
        let moniker = get_cml_moniker_from_query("123", &explorer).await.unwrap();
        assert_eq!(moniker, AbsoluteMoniker::parse_str("/core/foo").unwrap());
    }
}
