// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod copy;
mod delete;
mod list;
mod make_directory;

pub use {copy::copy, delete::delete, list::list, make_directory::make_directory};

#[cfg(test)]
pub mod test {
    use std::collections::HashMap;

    use {
        fidl::endpoints::{RequestStream, ServerEnd},
        fidl::handle::AsyncChannel,
        fidl_fuchsia_io as fio,
        fidl_fuchsia_sys2::StorageAdminProxy,
        fidl_fuchsia_sys2::StorageAdminRequest,
        futures::TryStreamExt,
        std::fs::{create_dir, write},
        tempfile::tempdir,
    };

    fn setup_oneshot_fake_storage_admin<R: 'static>(mut handle_request: R) -> StorageAdminProxy
    where
        R: FnMut(fidl::endpoints::Request<<StorageAdminProxy as fidl::endpoints::Proxy>::Protocol>),
    {
        let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<
            <StorageAdminProxy as fidl::endpoints::Proxy>::Protocol,
        >()
        .unwrap();
        fuchsia_async::Task::local(async move {
            if let Ok(Some(req)) = stream.try_next().await {
                handle_request(req);
            }
        })
        .detach();
        proxy
    }

    pub fn node_to_directory(object: ServerEnd<fio::NodeMarker>) -> fio::DirectoryRequestStream {
        fio::DirectoryRequestStream::from_channel(
            AsyncChannel::from_channel(object.into_channel()).unwrap(),
        )
    }

    pub fn node_to_file(object: ServerEnd<fio::NodeMarker>) -> fio::FileRequestStream {
        fio::FileRequestStream::from_channel(
            AsyncChannel::from_channel(object.into_channel()).unwrap(),
        )
    }

    pub fn setup_fake_storage_admin(
        expected_id: &'static str,
        setup_fake_directory_fn: fn(fio::DirectoryRequestStream),
    ) -> StorageAdminProxy {
        setup_oneshot_fake_storage_admin(move |req| match req {
            StorageAdminRequest::OpenComponentStorageById { id, object, responder, .. } => {
                assert_eq!(expected_id, id);
                setup_fake_directory_fn(node_to_directory(object));
                responder.send(&mut Ok(())).unwrap();
            }
            _ => panic!("got unexpected {:?}", req),
        })
    }

    // Create an arbitrary path string with tmp as the root.
    pub fn create_tmp_path_string() -> String {
        let tmp_dir = tempdir();
        let dir = tmp_dir.as_ref().unwrap();
        let tmp_path = String::from(dir.path().to_str().unwrap());
        tmp_dir.expect("Could not close file").close().unwrap();
        return tmp_path;
    }

    // Sets up a temporary directory path as the component storage's root.
    ///
    /// # Arguments
    /// * `expected_id`: Original static storage instance id to ensure tests setup and function copy is working with the same component
    /// * `seed_files`: HashMap of files and their contents that will be populated to a component's storage
    pub fn setup_fake_storage_admin_with_tmp(
        expected_id: &'static str,
        seed_files: HashMap<&'static str, &'static str>,
    ) -> StorageAdminProxy {
        setup_oneshot_fake_storage_admin(move |req| match req {
            StorageAdminRequest::OpenComponentStorageById { id, object, responder, .. } => {
                assert_eq!(expected_id, id);
                let tmp_path = create_tmp_path_string();
                let () = create_dir(&tmp_path).unwrap();

                for (new_file, new_file_contents) in seed_files.iter() {
                    let new_file_path = format!("{}/{}", tmp_path, new_file);
                    write(&new_file_path, new_file_contents).unwrap();
                }

                fuchsia_fs::directory::open_channel_in_namespace(
                    &tmp_path,
                    fio::OpenFlags::RIGHT_READABLE
                        | fio::OpenFlags::RIGHT_WRITABLE
                        | fio::OpenFlags::DIRECTORY,
                    ServerEnd::new(object.into_channel()),
                )
                .unwrap();
                responder.send(&mut Ok(())).unwrap();
            }
            _ => panic!("got unexpected {:?}", req),
        })
    }
}
