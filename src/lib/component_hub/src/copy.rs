// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        io::Directory,
        path::{finalize_destination_to_filepath, HostOrRemotePath, RemotePath, REMOTE_PATH_HELP},
    },
    anyhow::{anyhow, bail, Result},
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys,
    std::fs::write,
    std::path::PathBuf,
};

/// Transfer a file between a component's namespace to/from the host machine.
///
/// # Arguments
/// * `query`: |RealmQueryProxy| which will be used to fetch the component's namespace.
/// * `source_path`:  source path of either a host filepath or a component namespace entry.
/// * `destination_path`: destination path to a host filepath or a component namespace entry.
pub async fn copy(
    query: &fsys::RealmQueryProxy,
    source_path: String,
    destination_path: String,
) -> Result<()> {
    match (HostOrRemotePath::parse(&source_path), HostOrRemotePath::parse(&destination_path)) {
        (HostOrRemotePath::Remote(source), HostOrRemotePath::Host(destination)) => {
            let moniker = &source.remote_id;
            // A relative moniker is required for |fuchsia.sys2/RealmQuery.GetInstanceInfo|
            let relative_moniker = format!(".{moniker}");
            let (_, resolved_state) = match query.get_instance_info(&relative_moniker).await? {
                Ok((info, state)) => (info, state),
                Err(fsys::RealmQueryError::InstanceNotFound) => {
                    bail!("Could not find an instance with the moniker: {}\n\
                               Use `ffx component list` or `ffx component show` to find the correct moniker of your instance.",
                        &moniker
                    );
                }
                Err(e) => {
                    bail!(
                        "Encountered an unexpected error when looking for instance with the provider moniker: {:?}\n",
                        e
                    );
                }
            };
            // resolved_state is safe to unwrap as an error would be thrown otherwise in the above statement.
            let resolved_state = resolved_state.unwrap();
            let dir_proxy = (*resolved_state).ns_dir.into_proxy()?;
            copy_file_from_namespace(&dir_proxy, source, destination).await?;
            Ok(())
        }
        _ => {
            bail!("Currently only copying from Target to Host is supported. {}\n", REMOTE_PATH_HELP)
        }
    }
}

/// Writes file contents to a directory from a component's namespace.
///
/// # Arguments
/// * `component_namespace`: Directory proxy to component's namespace directory
/// * `source`: source path of target device
/// * `destination`: destination path of host
/// * `file_path`: path to file in target's namespace
pub async fn copy_file_from_namespace(
    component_namespace: &DirectoryProxy,
    source: RemotePath,
    destination: PathBuf,
) -> Result<()> {
    let file_path = source.relative_path.clone();
    let component_namespace = Directory::from_proxy(component_namespace.to_owned());
    let destination_path = finalize_destination_to_filepath(
        &component_namespace,
        HostOrRemotePath::Remote(source),
        HostOrRemotePath::Host(destination),
    )
    .await?;

    let data = component_namespace.read_file_bytes(file_path).await?;
    write(destination_path, data).map_err(|e| anyhow!("Could not write file to host: {:?}", e))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_utils::{serve_realm_query, serve_realm_query_with_namespace},
        fidl::endpoints::{create_endpoints, ClientEnd},
        fidl_fuchsia_io as fio,
        std::collections::HashMap,
        std::fs::read,
        std::path::Path,
        test_case::test_case,
    };

    const LARGE_FILE_ARRAY: [u8; 64000] = [b'a'; 64000];
    const OVER_LIMIT_FILE_ARRAY: [u8; 64001] = [b'a'; 64001];
    const SAMPLE_NAME: &str = "./core/appmgr";
    const SAMPLE_MONIKER: &str = "./core/appmgr";
    const SAMPLE_FILE_NAME: &str = "foo.txt";
    const SAMPLE_FILE_NAME_2: &str = "bar.txt";
    const SAMPLE_FILE_CONTENTS: &str = "Lorem Ipsum";

    fn create_resolved_state(
        exposed_dir: ClientEnd<fio::DirectoryMarker>,
        ns_dir: ClientEnd<fio::DirectoryMarker>,
    ) -> Option<Box<fsys::ResolvedState>> {
        Some(Box::new(fsys::ResolvedState {
            uses: vec![],
            exposes: vec![],
            config: None,
            pkg_dir: None,
            execution: Some(Box::new(fsys::ExecutionState {
                out_dir: None,
                runtime_dir: None,
                start_reason: "Debugging Workflow".to_string(),
            })),
            exposed_dir,
            ns_dir,
        }))
    }

    fn create_hashmap_of_instance_info(
        name: &str,
        moniker: &str,
        ns_dir: ClientEnd<fio::DirectoryMarker>,
    ) -> HashMap<String, (fsys::InstanceInfo, Option<Box<fsys::ResolvedState>>)> {
        let (exposed_dir, _) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        HashMap::from([(
            name.to_string(),
            (
                fsys::InstanceInfo {
                    moniker: moniker.to_string(),
                    url: String::new(),
                    instance_id: None,
                    state: fsys::InstanceState::Started,
                },
                create_resolved_state(exposed_dir, ns_dir),
            ),
        )])
    }

    #[test_case("/core/appmgr::foo.txt", "/hello/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)], "/hello/foo.txt"; "device_to_host")]
    #[test_case("/core/appmgr::foo.txt", "/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)], "/foo.txt"; "device_to_host_root")]
    #[test_case("/core/appmgr::foo.txt", "/hello", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)], "/hello/foo.txt"; "device_to_host_infer_path")]
    #[test_case("/core/appmgr::foo.txt", "/hello/", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)], "/hello/foo.txt"; "device_to_host_infer_slash_path")]
    #[test_case("/core/appmgr::foo.txt", "/hello/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS),
                                                                    (SAMPLE_FILE_NAME_2, SAMPLE_FILE_CONTENTS)], "/hello/foo.txt"; "device_to_host_populated_directory")]
    #[test_case("/core/appmgr::foo.txt", "/hello/foo.txt", vec![(SAMPLE_FILE_NAME, std::str::from_utf8(&LARGE_FILE_ARRAY).unwrap())], "/hello/foo.txt"; "device_to_host_large_file")]
    #[test_case("/core/appmgr::foo.txt", "/hello/foo.txt", vec![(SAMPLE_FILE_NAME, std::str::from_utf8(&OVER_LIMIT_FILE_ARRAY).unwrap())], "/hello/foo.txt"; "inconclusive device_to_host_over_file_limit")]
    #[fuchsia::test]
    async fn copy_from_device_to_host(
        source_path: &'static str,
        destination_path: &'static str,
        seed_files: Vec<(&'static str, &'static str)>,
        actual_data_path: &'static str,
    ) -> Result<()> {
        let (ns_dir, ns_server) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let seed_files =
            HashMap::from(seed_files.into_iter().collect::<HashMap<&'static str, &'static str>>());
        let () = serve_realm_query_with_namespace(ns_server, seed_files)?;
        let query_instance = create_hashmap_of_instance_info(SAMPLE_NAME, SAMPLE_MONIKER, ns_dir);
        let realm_query_proxy = serve_realm_query(query_instance);

        copy(&realm_query_proxy, source_path.to_owned(), destination_path.to_owned()).await?;

        let expected_data = SAMPLE_FILE_CONTENTS.to_owned().into_bytes();
        let actual_data_path = Path::new(actual_data_path);
        let actual_data = read(&actual_data_path).unwrap();
        assert_eq!(actual_data, expected_data);
        Ok(())
    }

    #[test_case("wrong_moniker/core/appmgr::foo.txt", "/hello/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)]; "bad_moniker")]
    #[test_case("/core/appmgr/foo.txt", "/hello/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)]; "host_to_host_not_supported")]
    #[test_case("/core/appmgr::bar.txt", "/hello/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)]; "bad_file")]
    #[test_case("/core/appmgr::foo.txt", "/hello/bar/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)]; "bad_directory")]
    #[fuchsia::test]
    async fn copy_from_device_to_host_fails(
        source_path: &'static str,
        destination_path: &'static str,
        seed_files: Vec<(&'static str, &'static str)>,
    ) -> Result<()> {
        let (ns_dir, ns_server) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let seed_files =
            HashMap::from(seed_files.into_iter().collect::<HashMap<&'static str, &'static str>>());
        let () = serve_realm_query_with_namespace(ns_server, seed_files)?;
        let query_instance = create_hashmap_of_instance_info(SAMPLE_NAME, SAMPLE_MONIKER, ns_dir);
        let realm_query_proxy = serve_realm_query(query_instance);
        let source_path = source_path.to_owned();

        let result = copy(&realm_query_proxy, source_path, destination_path.to_string()).await;

        assert!(result.is_err());
        Ok(())
    }
}
