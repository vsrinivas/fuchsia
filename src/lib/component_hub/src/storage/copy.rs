// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{HostOrRemotePath, REMOTE_PATH_HELP},
    crate::io::Directory,
    anyhow::{anyhow, bail, Error, Result},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2::StorageAdminProxy,
    fuchsia_fs::directory::DirentKind,
    std::fs::{read, write},
    std::path::PathBuf,
};

/// Transfer a file between the host machine and the Fuchsia device.
/// Can be used to upload a file to or from the Fuchsia device.
///
/// # Arguments
/// * `storage_admin`: The StorageAdminProxy.
/// * `source_path`: The path to a file on the host machine to be uploaded to the device or to a file on the device to be downloaded on the host machine
/// * `destination_path`: The path and filename on the target component or the host machine where to save the file
pub async fn copy(
    storage_admin: StorageAdminProxy,
    source_path: String,
    destination_path: String,
) -> Result<()> {
    let (dir_proxy, server) = create_proxy::<fio::DirectoryMarker>()?;
    let server = server.into_channel();
    let storage_dir = Directory::from_proxy(dir_proxy);

    match (HostOrRemotePath::parse(&source_path), HostOrRemotePath::parse(&destination_path)) {
        (HostOrRemotePath::Remote(source), HostOrRemotePath::Host(destination)) => {
            // Copying from remote to host
            storage_admin
                .open_component_storage_by_id(&source.instance_id, server.into())
                .await?
                .map_err(|e| anyhow!("Could not open component storage: {:?}", e))?;

            let remote_source_path = source.relative_path.clone();
            let host_destination_path = finalize_destination_to_filepath(
                HostOrRemotePath::Remote(source),
                HostOrRemotePath::Host(destination),
                &storage_dir,
            )
            .await?;

            let data = storage_dir.read_file_bytes(remote_source_path).await?;
            write(host_destination_path, data)
                .map_err(|e| anyhow!("Could not write file to host: {:?}", e))?;
            Ok(())
        }
        (HostOrRemotePath::Host(source), HostOrRemotePath::Remote(destination)) => {
            // Copying from host to remote
            storage_admin
                .open_component_storage_by_id(&destination.instance_id, server.into())
                .await?
                .map_err(|e| anyhow!("Could not open component storage: {:?}", e))?;

            let host_source_path = source.clone();
            let remote_destination_path = finalize_destination_to_filepath(
                HostOrRemotePath::Host(source),
                HostOrRemotePath::Remote(destination),
                &storage_dir,
            )
            .await?;

            let data = read(host_source_path)
                .map_err(|e| anyhow!("Could not read file from host: {:?}", e))?;
            storage_dir.create_file(remote_destination_path, data.as_slice()).await?;
            Ok(())
        }
        _ => {
            bail!("One path must be remote and the other must be host. {}", REMOTE_PATH_HELP)
        }
    }
}

/// Returns a valid file path to write to given a source and destination paths.
/// This function will return a new destination path if the final component in the path is a directory.
/// The new path will have the filename of the source path appended to the destination path.
/// Otherwise, the destination path passed in will be returned.
///
/// # Arguments
/// * `source_path`: The path to a file on the host machine to be uploaded to the device or to a file on the device to be downloaded on the host machine
/// * `destination_path`: The path and filename on the target component or the host machine where to save the file which may need to be inferred (may not end with a file name)
/// * `storage_dir`: Directory proxy to help retrieve directory entries on device
pub async fn finalize_destination_to_filepath(
    source: HostOrRemotePath,
    destination: HostOrRemotePath,
    storage_dir: &Directory,
) -> Result<PathBuf, Error> {
    let source_file = match source {
        HostOrRemotePath::Host(path) => path.file_name().map_or_else(
            || Err(anyhow!("Source path was unexpectedly empty")),
            |file| Ok(PathBuf::from(file)),
        )?,
        HostOrRemotePath::Remote(path) => path.relative_path.file_name().map_or_else(
            || Err(anyhow!("Source path was unexpectedly empty")),
            |file| Ok(PathBuf::from(file)),
        )?,
    };

    match destination {
        HostOrRemotePath::Host(mut destination_path) => {
            let destination_file = destination_path.file_name();

            if destination_file.is_none() || destination_path.is_dir() {
                destination_path.push(source_file.to_str().unwrap());
            }

            Ok(destination_path)
        }
        HostOrRemotePath::Remote(destination_path) => {
            let mut destination_path = destination_path.relative_path;
            let destination_file = destination_path.file_name();
            let remote_destination_entry = match destination_file.is_none() {
                true => storage_dir.entry_if_exists(source_file.to_str().unwrap()).await?,
                false => {
                    storage_dir
                        .entry_if_exists(
                            destination_file.unwrap_or_default().to_str().unwrap_or_default(),
                        )
                        .await?
                }
            };

            if remote_destination_entry.is_none() {
                match destination_file.is_none() {
                    true => {
                        destination_path.push(source_file.to_str().unwrap());
                        return Ok(destination_path);
                    }
                    _ => return Ok(destination_path),
                };
            }

            match remote_destination_entry.unwrap().kind {
                DirentKind::Directory => {
                    destination_path.push(source_file.to_str().unwrap());
                }

                DirentKind::File => {
                    return Err(anyhow!(
                        "File {} already exists in the directory {}",
                        source_file.to_str().unwrap(),
                        destination_path.display().to_string()
                    ));
                }

                _ => {
                    return Err(anyhow!(
                        "Invalid entry type for file {}",
                        source_file.to_str().unwrap()
                    ));
                }
            }
            Ok(destination_path)
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::storage::test::{
            node_to_file, setup_fake_storage_admin, setup_fake_storage_admin_with_tmp,
        },
        fidl_fuchsia_io as fio,
        futures::TryStreamExt,
        std::collections::HashMap,
        std::fs::{read, write},
        tempfile::tempdir,
    };

    const DATA: [u8; 4] = [0x0, 0x1, 0x2, 0x3];

    // TODO(xbhatnag): Replace this mock with something more robust like VFS.
    // Currently VFS is not cross-platform.
    fn setup_fake_directory(mut root_dir: fio::DirectoryRequestStream) {
        fuchsia_async::Task::local(async move {
            // Serve the root directory
            // Rewind on root directory should succeed
            let request = root_dir.try_next().await;
            if let Ok(Some(fio::DirectoryRequest::Open { path, flags, mode, object, .. })) = request
            {
                if path == "from_host" {
                    assert!(flags.intersects(fio::OpenFlags::CREATE));
                    assert!(mode & fio::MODE_TYPE_FILE != 0);
                    setup_fake_file_from_host(node_to_file(object));
                } else if path == "from_device" {
                    assert!(mode & fio::MODE_TYPE_FILE != 0);
                    setup_fake_file_from_device(node_to_file(object));
                } else {
                    panic!("incorrect path: {}", path);
                }
            } else {
                panic!("did not get open request: {:?}", request)
            }
        })
        .detach();
    }

    fn setup_fake_file_from_host(mut file: fio::FileRequestStream) {
        fuchsia_async::Task::local(async move {
            // Serve the root directory
            // Truncating the file should succeed
            let request = file.try_next().await;
            if let Ok(Some(fio::FileRequest::Resize { length, responder })) = request {
                assert_eq!(length, 0);
                responder.send(&mut Ok(())).unwrap();
            } else {
                panic!("did not get resize request: {:?}", request)
            }

            // Writing the file should succeed
            let request = file.try_next().await;
            if let Ok(Some(fio::FileRequest::Write { data, responder })) = request {
                assert_eq!(data, DATA);
                responder.send(&mut Ok(data.len() as u64)).unwrap();
            } else {
                panic!("did not get write request: {:?}", request)
            }

            // Closing file should succeed
            let request = file.try_next().await;
            if let Ok(Some(fio::FileRequest::Close { responder })) = request {
                responder.send(&mut Ok(())).unwrap();
            } else {
                panic!("did not get close request: {:?}", request)
            }
        })
        .detach();
    }

    fn setup_fake_file_from_device(mut file: fio::FileRequestStream) {
        fuchsia_async::Task::local(async move {
            // Serve the root directory
            // Reading the file should succeed
            let request = file.try_next().await;
            if let Ok(Some(fio::FileRequest::Read { responder, .. })) = request {
                responder.send(&mut Ok(DATA.to_vec())).unwrap();
            } else {
                panic!("did not get read request: {:?}", request)
            }

            // Reading the file should not return any more data
            let request = file.try_next().await;
            if let Ok(Some(fio::FileRequest::Read { responder, .. })) = request {
                responder.send(&mut Ok(vec![])).unwrap();
            } else {
                panic!("did not get read request: {:?}", request)
            }

            // Closing file should succeed
            let request = file.try_next().await;
            if let Ok(Some(fio::FileRequest::Close { responder })) = request {
                responder.send(&mut Ok(())).unwrap();
            } else {
                panic!("did not get close request: {:?}", request)
            }
        })
        .detach();
    }

    #[fuchsia::test]
    async fn test_copy_host_to_device() -> Result<()> {
        let dir = tempdir().unwrap();
        let storage_admin = setup_fake_storage_admin_with_tmp("123456", HashMap::new());
        let from_host_filepath = dir.path().join("from_host");
        write(&from_host_filepath, &DATA).unwrap();
        copy(
            storage_admin,
            from_host_filepath.display().to_string(),
            "123456::from_host".to_string(),
        )
        .await
    }

    #[fuchsia::test]
    async fn test_copy_host_to_device_different_file_names() -> Result<()> {
        let dir = tempdir().unwrap();
        let storage_admin = setup_fake_storage_admin_with_tmp("123456", HashMap::new());
        let from_host_filepath = dir.path().join("from_host");
        write(&from_host_filepath, &DATA).unwrap();
        copy(
            storage_admin,
            from_host_filepath.display().to_string(),
            "123456::from_host_test".to_string(),
        )
        .await
    }

    #[fuchsia::test]
    async fn test_copy_host_to_device_infer_path() -> Result<()> {
        let dir = tempdir().unwrap();
        let storage_admin = setup_fake_storage_admin_with_tmp("123456", HashMap::new());
        let from_host_filepath = dir.path().join("from_host");
        write(&from_host_filepath, &DATA).unwrap();
        copy(storage_admin, from_host_filepath.display().to_string(), "123456::".to_string()).await
    }

    #[fuchsia::test]
    async fn test_copy_host_to_device_infer_slash_path() -> Result<()> {
        let dir = tempdir().unwrap();
        let storage_admin = setup_fake_storage_admin_with_tmp("123456", HashMap::new());
        let from_host_filepath = dir.path().join("from_host");
        write(&from_host_filepath, &DATA).unwrap();
        copy(storage_admin, from_host_filepath.display().to_string(), "123456::/".to_string()).await
    }

    #[fuchsia::test]
    async fn test_copy_host_to_device_populated_directory() -> Result<()> {
        let dir = tempdir().unwrap();
        let mut seed_files = HashMap::new();

        seed_files.insert("foo.txt", "Lorem Ipsum");

        let storage_admin = setup_fake_storage_admin_with_tmp("123456", seed_files);
        let from_host_filepath = dir.path().join("from_host");
        write(&from_host_filepath, &DATA).unwrap();
        copy(
            storage_admin,
            from_host_filepath.display().to_string(),
            "123456::from_host".to_string(),
        )
        .await
    }

    #[fuchsia::test]
    async fn test_copy_device_to_host_infer_path() -> Result<()> {
        let dir = tempdir().unwrap();
        let storage_admin = setup_fake_storage_admin("123456", setup_fake_directory);
        let dest_filepath = dir.path();

        copy(storage_admin, "123456::from_device".to_string(), dest_filepath.display().to_string())
            .await?;

        let final_path = dest_filepath.join("from_device");
        let data = read(final_path).unwrap();
        assert_eq!(data, DATA);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_copy_device_to_host_infer_slash_path() -> Result<()> {
        let dir = tempdir().unwrap();
        let storage_admin = setup_fake_storage_admin("123456", setup_fake_directory);
        let dest_filepath = dir.path();

        copy(
            storage_admin,
            "123456::from_device".to_string(),
            dest_filepath.display().to_string() + "/",
        )
        .await?;

        let final_path = dest_filepath.join("from_device");
        let data = read(final_path).unwrap();
        assert_eq!(data, DATA);
        Ok(())
    }

    #[fuchsia::test]
    async fn test_copy_device_to_host() -> Result<()> {
        let dir = tempdir().unwrap();
        let storage_admin = setup_fake_storage_admin("123456", setup_fake_directory);
        let dest_filepath = dir.path().join("from_device");
        copy(storage_admin, "123456::from_device".to_string(), dest_filepath.display().to_string())
            .await?;
        let data = read(dest_filepath).unwrap();
        assert_eq!(data, DATA);
        Ok(())
    }
}
