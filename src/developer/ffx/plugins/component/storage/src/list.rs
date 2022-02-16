// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::RemotePath, anyhow::Result, component_hub::io::Directory, errors::ffx_error,
    ffx_component_storage_args::ListArgs, fidl::endpoints::create_proxy, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2::StorageAdminProxy,
};

pub async fn list(storage_admin: StorageAdminProxy, args: ListArgs) -> Result<()> {
    list_cmd(storage_admin, args.path, &mut std::io::stdout()).await
}

async fn list_cmd<W: std::io::Write>(
    storage_admin: StorageAdminProxy,
    path: String,
    mut writer: W,
) -> Result<()> {
    let remote_path = RemotePath::parse(&path)?;

    let (dir_proxy, server) = create_proxy::<fio::DirectoryMarker>()?;
    let server = server.into_channel();
    let storage_dir = Directory::from_proxy(dir_proxy);

    storage_admin
        .open_component_storage_by_id(&remote_path.component_instance_id, server.into())
        .await?
        .map_err(|e| ffx_error!("Could not open component storage: {:?}", e))?;

    let dir = if remote_path.relative_path.as_os_str().is_empty() {
        storage_dir
    } else {
        storage_dir.open_dir_readable(remote_path.relative_path)?
    };

    let entries = dir.entries().await?;
    for entry in entries {
        writeln!(writer, "{}", entry)?;
    }
    Ok(())
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::test::setup_oneshot_fake_storage_admin,
        fidl::endpoints::{RequestStream, ServerEnd},
        fidl::handle::AsyncChannel,
        fidl_fuchsia_io::*,
        fidl_fuchsia_sys2::StorageAdminRequest,
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
            bytes.push(DIRENT_TYPE_FILE);
            // name: [u8]
            for byte in name.bytes() {
                bytes.push(byte);
            }
        }
        bytes
    }

    pub fn node_to_directory(object: ServerEnd<NodeMarker>) -> DirectoryRequestStream {
        DirectoryRequestStream::from_channel(
            AsyncChannel::from_channel(object.into_channel()).unwrap(),
        )
    }

    fn setup_fake_storage_admin(expected_id: &'static str) -> StorageAdminProxy {
        setup_oneshot_fake_storage_admin(move |req| match req {
            StorageAdminRequest::OpenComponentStorageById { id, object, responder, .. } => {
                assert_eq!(expected_id, id);
                setup_fake_directory(node_to_directory(object));
                responder.send(&mut Ok(())).unwrap();
            }
            _ => panic!("got unexpected {:?}", req),
        })
    }

    // TODO(xbhatnag): Replace this mock with something more robust like VFS.
    // Currently VFS is not cross-platform.
    fn setup_fake_directory(mut root_dir: DirectoryRequestStream) {
        fuchsia_async::Task::local(async move {
            let dirents = dirents(vec!["foo", "bar"]);

            // Serve the root directory
            // Rewind on root directory should succeed
            let request = root_dir.try_next().await;
            if let Ok(Some(DirectoryRequest::Rewind { responder, .. })) = request {
                responder.send(0).unwrap();
            } else {
                panic!("did not get rewind request: {:?}", request)
            }

            // ReadDirents should report two files in the root directory
            let request = root_dir.try_next().await;
            if let Ok(Some(DirectoryRequest::ReadDirents { max_bytes, responder })) = request {
                assert!(dirents.len() as u64 <= max_bytes);
                responder.send(0, dirents.as_slice()).unwrap();
            } else {
                panic!("did not get readdirents request: {:?}", request)
            }

            // ReadDirents should not report any more contents
            let request = root_dir.try_next().await;
            if let Ok(Some(DirectoryRequest::ReadDirents { responder, .. })) = request {
                responder.send(0, &[]).unwrap();
            } else {
                panic!("did not get readdirents request: {:?}", request)
            }
        })
        .detach();
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_root() -> Result<()> {
        let mut output_utf8 = Vec::new();
        let storage_admin = setup_fake_storage_admin("123456");
        list_cmd(storage_admin, "123456::.".to_string(), &mut output_utf8).await?;

        let output = String::from_utf8(output_utf8).expect("Invalid UTF-8 bytes");
        assert!(output.contains("foo"));
        assert!(output.contains("bar"));
        Ok(())
    }
}
