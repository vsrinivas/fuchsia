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
    std::fs::{read, write},
    std::path::PathBuf,
};

const CHANNEL_SIZE_LIMIT: u64 = 64 * 1024;

/// Transfer a file between a component's namespace to/from the host machine.
///
/// # Arguments
/// * `realm_query`: |RealmQueryProxy| to fetch the component's namespace.
/// * `source_path`: A path containing either a host filepath or a component namespace entry to retrieve file contents.
/// * `destination_path`: A path to a host filepath or a component namespace entry to copy over file contents.
pub async fn copy(
    realm_query: &fsys::RealmQueryProxy,
    source_path: String,
    destination_path: String,
) -> Result<()> {
    match (HostOrRemotePath::parse(&source_path), HostOrRemotePath::parse(&destination_path)) {
        (HostOrRemotePath::Remote(source), HostOrRemotePath::Host(destination)) => {
            let namespace = retrieve_namespace(realm_query, &source.remote_id).await?;
            copy_file_from_namespace(&namespace, source, destination).await?;
            Ok(())
        }
        (HostOrRemotePath::Host(source), HostOrRemotePath::Remote(destination)) => {
            let namespace = retrieve_namespace(realm_query, &destination.remote_id).await?;
            copy_file_to_namespace(&namespace, source, destination).await?;
            Ok(())
        }
        _ => {
            bail!("Currently only copying from Target to Host is supported. {}\n", REMOTE_PATH_HELP)
        }
    }
}

/// Retrieves the directory proxy associated with a component's namespace
/// # Arguments
/// * `realm_query`: |RealmQueryProxy| to retrieve a component instance.
/// * `moniker`: Absolute moniker of a component instance.
pub async fn retrieve_namespace(
    realm_query: &fsys::RealmQueryProxy,
    moniker: &str,
) -> Result<DirectoryProxy> {
    // A relative moniker is required for |fuchsia.sys2/RealmQuery.GetInstanceInfo|
    let relative_moniker = format!(".{moniker}");
    let (_, resolved_state) = match realm_query.get_instance_info(&relative_moniker).await? {
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
    let namespace = (*resolved_state).ns_dir.into_proxy()?;
    Ok(namespace)
}

/// Writes file contents to a directory to a component's namespace.
///
/// # Arguments
/// * `namespace`: A proxy to the component's namespace directory.
/// * `source`: The host filepath.
/// * `destination`: The path of a component namespace entry.
pub async fn copy_file_to_namespace(
    namespace: &DirectoryProxy,
    source: PathBuf,
    destination: RemotePath,
) -> Result<()> {
    let file_path = source.clone();
    let namespace = Directory::from_proxy(namespace.to_owned());
    let destination_path = finalize_destination_to_filepath(
        &namespace,
        HostOrRemotePath::Host(source),
        HostOrRemotePath::Remote(destination),
    )
    .await?;
    let data = read(file_path)?;
    namespace.verify_directory_is_read_write(&destination_path.parent().unwrap()).await?;
    namespace.create_file(destination_path, data.as_slice()).await?;
    Ok(())
}

/// Writes file contents to a directory from a component's namespace.
///
/// # Arguments
/// * `namespace`: A proxy to the component's namespace directory.
/// * `source`: The path of a component namespace entry.
/// * `destination`: The host filepath.
pub async fn copy_file_from_namespace(
    namespace: &DirectoryProxy,
    source: RemotePath,
    destination: PathBuf,
) -> Result<()> {
    let file_path = source.relative_path.clone();
    let namespace = Directory::from_proxy(namespace.to_owned());
    let destination_path = finalize_destination_to_filepath(
        &namespace,
        HostOrRemotePath::Remote(source),
        HostOrRemotePath::Host(destination),
    )
    .await?;
    let file_size = namespace.get_file_size(&file_path).await?;
    // TODO(http://fxbug.dev/111473): Add large file support
    if file_size > CHANNEL_SIZE_LIMIT {
        return Err(anyhow!(
            "File: \"{}\" is greater than 64KB which is currently not supported.",
            { file_path.display() }
        ));
    }

    let data = namespace.read_file_bytes(file_path).await?;
    write(destination_path, data).map_err(|e| anyhow!("Could not write file to host: {:?}", e))?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::test_utils::{
            duplicate_namespace_client, populate_host_with_file_contents, read_data_from_namespace,
            serve_realm_query, serve_realm_query_with_namespace, set_path_to_read_only,
        },
        fidl::endpoints::{create_endpoints, create_proxy, ClientEnd, Proxy},
        fidl_fuchsia_io as fio,
        std::collections::HashMap,
        std::fs::read,
        std::path::Path,
        tempfile::tempdir,
        test_case::test_case,
    };

    const LARGE_FILE_ARRAY: [u8; CHANNEL_SIZE_LIMIT as usize] = [b'a'; CHANNEL_SIZE_LIMIT as usize];
    const OVER_LIMIT_FILE_ARRAY: [u8; (CHANNEL_SIZE_LIMIT + 1) as usize] =
        [b'a'; (CHANNEL_SIZE_LIMIT + 1) as usize];
    const SAMPLE_NAME: &str = "./core/appmgr";
    const SAMPLE_MONIKER: &str = "./core/appmgr";
    const SAMPLE_FILE_NAME: &str = "foo.txt";
    const SAMPLE_FILE_NAME_2: &str = "bar.txt";
    const SAMPLE_FILE_CONTENTS: &str = "Lorem Ipsum";
    const SAMPLE_FILE_CONTENTS_2: &str = "New Data";
    const READ_WRITE: bool = false;
    const READ_ONLY: bool = true;

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

    fn create_realm_query(
        seed_files: Vec<(&'static str, &'static str)>,
        is_read_only: bool,
    ) -> fsys::RealmQueryProxy {
        let (ns_dir, ns_server) = create_endpoints::<fio::DirectoryMarker>().unwrap();
        let seed_files =
            HashMap::from(seed_files.into_iter().collect::<HashMap<&'static str, &'static str>>());
        let () = serve_realm_query_with_namespace(ns_server, seed_files, is_read_only).unwrap();
        let query_instance = create_hashmap_of_instance_info(SAMPLE_NAME, SAMPLE_MONIKER, ns_dir);
        serve_realm_query(query_instance)
    }

    fn create_realm_query_with_ns_client(
        seed_files: Vec<(&'static str, &'static str)>,
        is_read_only: bool,
    ) -> (fsys::RealmQueryProxy, DirectoryProxy) {
        let (ns_dir, ns_server) = create_proxy::<fio::DirectoryMarker>().unwrap();
        let dup_client = duplicate_namespace_client(&ns_dir).unwrap();
        let seed_files =
            HashMap::from(seed_files.into_iter().collect::<HashMap<&'static str, &'static str>>());
        let () = serve_realm_query_with_namespace(ns_server, seed_files, is_read_only).unwrap();
        let ns_dir = ClientEnd::<fio::DirectoryMarker>::new(ns_dir.into_channel().unwrap().into());
        let query_instance = create_hashmap_of_instance_info(SAMPLE_NAME, SAMPLE_MONIKER, ns_dir);
        let realm_query = serve_realm_query(query_instance);

        (realm_query, dup_client)
    }

    #[test_case("/core/appmgr::/data/foo.txt", "/foo.txt", vec![], vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)], "/foo.txt", SAMPLE_FILE_CONTENTS; "device_to_host")]
    #[test_case("/core/appmgr::/data/foo.txt", "/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)], vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS_2)], "/foo.txt", SAMPLE_FILE_CONTENTS_2; "device_to_host_overwrite_file")]
    #[test_case("/core/appmgr::/data/foo.txt", "/bar.txt", vec![],  vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)], "/bar.txt", SAMPLE_FILE_CONTENTS; "device_to_host_different_name")]
    #[test_case("/core/appmgr::/data/foo.txt", "", vec![],  vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)], "/foo.txt", SAMPLE_FILE_CONTENTS; "device_to_host_infer_path")]
    #[test_case("/core/appmgr::/data/foo.txt", "/", vec![],  vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)], "/foo.txt", SAMPLE_FILE_CONTENTS; "device_to_host_infer_slash_path")]
    #[test_case("/core/appmgr::/data/foo.txt", "/foo.txt", vec![],  vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS),(SAMPLE_FILE_NAME_2, SAMPLE_FILE_CONTENTS)],
    "/foo.txt", SAMPLE_FILE_CONTENTS; "device_to_host_populated_directory")]
    #[test_case("/core/appmgr::/data/foo.txt", "/foo.txt", vec![],  vec![(SAMPLE_FILE_NAME, std::str::from_utf8(&LARGE_FILE_ARRAY).unwrap())], "/foo.txt", std::str::from_utf8(&LARGE_FILE_ARRAY).unwrap(); "device_to_host_large_file")]
    #[test_case("/core/appmgr::/data/foo.txt", "/foo.txt", vec![],  vec![(SAMPLE_FILE_NAME, std::str::from_utf8(&OVER_LIMIT_FILE_ARRAY).unwrap())], "/tmp/foo.txt", std::str::from_utf8(&LARGE_FILE_ARRAY).unwrap(); "inconclusive device_to_host_over_file_limit")]
    #[fuchsia::test]
    async fn copy_device_to_host(
        source_path: &'static str,
        destination_path: &'static str,
        host_seed_files: Vec<(&'static str, &'static str)>,
        device_seed_files: Vec<(&'static str, &'static str)>,
        actual_data_path: &'static str,
        expected_data: &'static str,
    ) {
        let root = tempdir().unwrap();
        let root_path = root.path().to_str().unwrap();
        let destination_path = format!("{}{}", root_path, destination_path);
        populate_host_with_file_contents(&root_path, host_seed_files).unwrap();
        let realm_query = create_realm_query(device_seed_files, READ_ONLY);

        copy(&realm_query, source_path.to_owned(), destination_path.to_owned()).await.unwrap();

        let expected_data = expected_data.to_owned().into_bytes();
        let actual_data_path_string = format!("{}{}", root_path, actual_data_path);
        let actual_data_path = Path::new(&actual_data_path_string);
        let actual_data = read(actual_data_path).unwrap();
        assert_eq!(actual_data, expected_data);
    }

    #[test_case("wrong_moniker/core/appmgr::/data/foo.txt", "/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)]; "bad_moniker")]
    #[test_case("/core/appmgr::/data/foo.txt", "/core/appmgr::/data/foo.txt",  vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)]; "device_to_device_not_supported")]
    #[test_case("/core/appmgr::/data/bar.txt", "/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)]; "bad_file")]
    #[test_case("/core/appmgr::/data/foo.txt", "/bar/foo.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS)]; "bad_directory")]
    #[test_case("/core/appmgr::/data/foo.txt", "/foo.txt", vec![(SAMPLE_FILE_NAME, std::str::from_utf8(&OVER_LIMIT_FILE_ARRAY).unwrap())]; "device_to_host_over_file_limit")]
    #[fuchsia::test]
    async fn copy_device_to_host_fails(
        source_path: &'static str,
        destination_path: &'static str,
        seed_files: Vec<(&'static str, &'static str)>,
    ) {
        let root = tempdir().unwrap();
        let root_path = root.path().to_str().unwrap();
        let destination_path = format!("{}{}", root_path, destination_path);
        let realm_query = create_realm_query(seed_files, READ_ONLY);

        let result = copy(&realm_query, source_path.to_owned(), destination_path.to_string()).await;

        assert!(result.is_err());
    }

    #[test_case("/core/appmgr::/data/foo.txt", "/"; "read_only_root")]
    #[fuchsia::test]
    async fn copy_device_to_host_fails_read_only(
        source_path: &'static str,
        destination_path: &'static str,
    ) {
        let root = tempdir().unwrap();
        let root_path = root.path().to_str().unwrap();
        let destination_path = format!("{}{}", root_path, destination_path);
        set_path_to_read_only(PathBuf::from(&destination_path)).unwrap();
        let realm_query = create_realm_query(vec![], READ_ONLY);

        let result = copy(&realm_query, source_path.to_owned(), destination_path.to_string()).await;

        assert!(result.is_err());
    }

    #[test_case("/foo.txt", "/core/appmgr::/data/foo.txt", vec![],  "/data/foo.txt", SAMPLE_FILE_CONTENTS; "host_to_device")]
    #[test_case("/foo.txt", "/core/appmgr::/data/bar.txt", vec![(SAMPLE_FILE_NAME, SAMPLE_FILE_CONTENTS_2)], "/data/bar.txt", SAMPLE_FILE_CONTENTS; "host_to_device_different_name")]
    #[test_case("/foo.txt", "/core/appmgr::/data/foo.txt", vec![],  "/data/foo.txt", SAMPLE_FILE_CONTENTS; "host_to_device_overwrite_file")]
    #[test_case("/foo.txt", "/core/appmgr::/data", vec![], "/data/foo.txt", SAMPLE_FILE_CONTENTS; "host_to_device_inferred_path")]
    #[test_case("/foo.txt", "/core/appmgr::/data/", vec![], "/data/foo.txt", SAMPLE_FILE_CONTENTS; "host_to_device_inferred_slash_path")]
    #[test_case("/foo.txt", "/core/appmgr::/data/", vec![], "/data/foo.txt", std::str::from_utf8(&LARGE_FILE_ARRAY).unwrap(); "host_to_device_large_file")]
    #[test_case("/foo.txt", "/core/appmgr::/data/", vec![], "/data/foo.txt", std::str::from_utf8(&OVER_LIMIT_FILE_ARRAY).unwrap(); "inconclusive host_to_device_over_limit_file")]
    #[fuchsia::test]
    async fn copy_host_to_device(
        source_path: &'static str,
        destination_path: &'static str,
        seed_files: Vec<(&'static str, &'static str)>,
        actual_data_path: &'static str,
        expected_data: &'static str,
    ) {
        let root = tempdir().unwrap();
        let root_path = root.path().to_str().unwrap();
        let source_path = format!("{}{}", root_path, source_path);
        write(&source_path, expected_data.to_owned().into_bytes()).unwrap();
        let (realm_query, ns_dir) = create_realm_query_with_ns_client(seed_files, READ_WRITE);

        copy(&realm_query, source_path.to_owned(), destination_path.to_owned()).await.unwrap();

        let actual_data = read_data_from_namespace(&ns_dir, actual_data_path).await.unwrap();
        let expected_data = expected_data.to_owned().into_bytes();
        assert_eq!(actual_data, expected_data);
    }

    #[test_case("/foo.txt", "/core/appmgr::/foo.txt", SAMPLE_FILE_CONTENTS; "root_dir")]
    #[test_case("/foo.txt", "/core/appmgr::", SAMPLE_FILE_CONTENTS; "root_dir_infer_path")]
    #[test_case("/foo.txt", "/core/appmgr::/", SAMPLE_FILE_CONTENTS; "root_dir_infer_path_slash")]
    #[test_case("/foo.txt", "wrong_moniker/core/appmgr::/data/foo.txt", SAMPLE_FILE_CONTENTS; "bad_moniker")]
    #[test_case("/foo.txt", "/core/appmgr::/bar/foo.txt", SAMPLE_FILE_CONTENTS; "bad_directory")]
    #[test_case("/foo.txt", "/core/appmgr/data/foo.txt", SAMPLE_FILE_CONTENTS; "host_to_host_not_supported")]
    #[test_case("/foo.txt", "/core/appmgr::/foo.txt", std::str::from_utf8(&OVER_LIMIT_FILE_ARRAY).unwrap(); "device_to_host_over_file_limit")]
    #[fuchsia::test]
    async fn copy_host_to_device_fails(
        source_path: &'static str,
        destination_path: &'static str,
        source_data: &'static str,
    ) {
        let root = tempdir().unwrap();
        let root_path = root.path().to_str().unwrap();
        let source_path = format!("{}{}", root_path, source_path);
        write(&source_path, source_data.to_owned().into_bytes()).unwrap();
        let realm_query = create_realm_query(vec![], READ_WRITE);

        let result = copy(&realm_query, source_path.to_owned(), destination_path.to_owned()).await;

        assert!(result.is_err());
    }

    #[test_case("/foo.txt", "/core/appmgr::/read_only/foo.txt", SAMPLE_FILE_CONTENTS; "read_only_folder")]
    #[test_case("/foo.txt", "/core/appmgr::/read_only", SAMPLE_FILE_CONTENTS; "read_only_folder_infer_path")]
    #[test_case("/foo.txt", "/core/appmgr::/read_only/", SAMPLE_FILE_CONTENTS; "read_only_folder_infer_path_slash")]
    #[fuchsia::test]
    async fn copy_host_to_device_fails_read_only(
        source_path: &'static str,
        destination_path: &'static str,
        source_data: &'static str,
    ) {
        let root = tempdir().unwrap();
        let root_path = root.path().to_str().unwrap();
        let source_path = format!("{}{}", root_path, source_path);
        write(&source_path, source_data.to_owned().into_bytes()).unwrap();
        let realm_query = create_realm_query(vec![], READ_ONLY);

        let result = copy(&realm_query, source_path.to_owned(), destination_path.to_owned()).await;

        assert!(result.is_err());
    }
}
