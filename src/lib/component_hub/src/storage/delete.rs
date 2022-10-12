// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{io::Directory, path::RemotePath},
    anyhow::{anyhow, Result},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2::StorageAdminProxy,
};

/// Delete a file component's storage.
///
/// # Arguments
/// * `storage_admin`: The StorageAdminProxy
/// * `path`: The name of a file on the target component
pub async fn delete(storage_admin: StorageAdminProxy, path: String) -> Result<()> {
    let remote_path = RemotePath::parse(&path)?;

    let (dir_proxy, server) = create_proxy::<fio::DirectoryMarker>()?;
    let server = server.into_channel();
    let storage_dir = Directory::from_proxy(dir_proxy);

    storage_admin
        .open_component_storage_by_id(&remote_path.remote_id, server.into())
        .await?
        .map_err(|e| anyhow!("Could not open component storage: {:?}", e))?;

    if remote_path.relative_path.as_os_str().is_empty() {
        return Err(anyhow!("can't delete empty path"));
    };

    let path_str = match remote_path.relative_path.to_str() {
        Some(p) => p,
        None => return Err(anyhow!("error parsing `{}`", &remote_path.relative_path.display())),
    };

    if !storage_dir.exists(&path_str).await? {
        return Err(anyhow!("file does not exist: {}", &path_str));
    }

    storage_dir.remove(&path_str).await?;

    println!("Deleted {}", &path_str);
    Ok(())
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

    fn setup_fake_directory(mut root_dir: fio::DirectoryRequestStream) {
        fuchsia_async::Task::local(async move {
            let dirents = dirents(vec!["foo", "bar"]);

            // Rewind on root directory should succeed.
            let request = root_dir.try_next().await;
            if let Ok(Some(fio::DirectoryRequest::Rewind { responder, .. })) = request {
                responder.send(0).unwrap();
            } else {
                panic!("did not get rewind request: {:?}", request)
            }

            // ReadDirents should report two files in the root directory.
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
                panic!("did not get 2nd readdirents request: {:?}", request)
            }

            match root_dir.try_next().await {
                Ok(Some(fio::DirectoryRequest::Unlink { name: a, options: o, responder })) => {
                    assert_eq!(a, "foo");
                    assert_eq!(o, fio::UnlinkOptions::EMPTY);
                    responder.send(&mut Ok(())).unwrap();
                }
                request => {
                    panic!("did not get delete request; received: {:?}", request)
                }
            }
        })
        .detach();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_delete_file() -> Result<()> {
        let storage_admin = setup_fake_storage_admin("123456", setup_fake_directory);
        delete(storage_admin.clone(), "123456::foo".to_string()).await
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_delete_file_no_file() -> Result<()> {
        let storage_admin = setup_fake_storage_admin("123456", setup_fake_directory);
        match delete(storage_admin.clone(), "123456::nope".to_string()).await {
            Err(e) => {
                assert_eq!(e.to_string(), "file does not exist: nope");
                Ok(())
            }
            Ok(()) => panic!("did not receive expected no-file error"),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_delete_file_empty_path() -> Result<()> {
        let storage_admin = setup_fake_storage_admin("123456", setup_fake_directory);
        match delete(storage_admin.clone(), "123456::".to_string()).await {
            Err(e) => {
                assert_eq!(e.to_string(), "can't delete empty path");
                Ok(())
            }
            Ok(()) => panic!("did not receive expected empty-path error"),
        }
    }
}
