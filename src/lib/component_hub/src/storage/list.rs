// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{io::Directory, path::RemotePath},
    anyhow::{anyhow, Result},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2::StorageAdminProxy,
};

/// List all directories and files in a component's storage.
/// Returns a vector of names of the directories and files as strings.
///
/// # Arguments
/// * `storage_admin`: The StorageAdminProxy
/// * `path`: A path on the target component
pub async fn list(storage_admin: StorageAdminProxy, path: String) -> Result<Vec<String>> {
    let remote_path = RemotePath::parse(&path)?;

    let (dir_proxy, server) = create_proxy::<fio::DirectoryMarker>()?;
    let server = server.into_channel();
    let storage_dir = Directory::from_proxy(dir_proxy);

    storage_admin
        .open_component_storage_by_id(&remote_path.remote_id, server.into())
        .await?
        .map_err(|e| anyhow!("Could not open component storage: {:?}", e))?;

    let dir = if remote_path.relative_path.as_os_str().is_empty() {
        storage_dir
    } else {
        storage_dir.open_dir_readable(remote_path.relative_path)?
    };

    dir.entry_names().await
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*, crate::storage::test::setup_fake_storage_admin, fidl_fuchsia_io as fio,
        futures::TryStreamExt,
    };

    pub fn dirents(names: Vec<&'static str>) -> Vec<u8> {
        let mut bytes = vec![];
        for name in names {
            // inode: u64
            for _ in 0..8 {
                bytes.push(0);
            }
            // size: u8
            bytes.push(name.len() as u8);
            // type: u8
            bytes.push(fio::DirentType::File as u8);
            // name: [u8]
            for byte in name.bytes() {
                bytes.push(byte);
            }
        }
        bytes
    }

    // TODO(xbhatnag): Replace this mock with something more robust like VFS.
    // Currently VFS is not cross-platform.
    fn setup_fake_directory(mut root_dir: fio::DirectoryRequestStream) {
        fuchsia_async::Task::local(async move {
            let dirents = dirents(vec!["foo", "bar"]);

            // Serve the root directory
            // Rewind on root directory should succeed
            let request = root_dir.try_next().await;
            if let Ok(Some(fio::DirectoryRequest::Rewind { responder, .. })) = request {
                responder.send(0).unwrap();
            } else {
                panic!("did not get rewind request: {:?}", request)
            }

            // ReadDirents should report two files in the root directory
            let request = root_dir.try_next().await;
            if let Ok(Some(fio::DirectoryRequest::ReadDirents { max_bytes, responder })) = request {
                assert!(dirents.len() as u64 <= max_bytes);
                responder.send(0, dirents.as_slice()).unwrap();
            } else {
                panic!("did not get readdirents request: {:?}", request)
            }

            // ReadDirents should not report any more contents
            let request = root_dir.try_next().await;
            if let Ok(Some(fio::DirectoryRequest::ReadDirents { responder, .. })) = request {
                responder.send(0, &[]).unwrap();
            } else {
                panic!("did not get readdirents request: {:?}", request)
            }
        })
        .detach();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_root() -> Result<()> {
        let storage_admin = setup_fake_storage_admin("123456", setup_fake_directory);
        let dir_entries = list(storage_admin, "123456::.".to_string()).await?;

        assert_eq!(dir_entries, vec!["bar", "foo"]);
        Ok(())
    }
}
