// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::Result,
    fidl::endpoints::{create_proxy_and_stream, ServerEnd},
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys,
    fuchsia_async::Task,
    futures::StreamExt,
    std::collections::HashMap,
    std::fs::{create_dir, write},
    tempfile::tempdir,
};

pub fn serve_realm_query(
    mut instances: HashMap<String, (fsys::InstanceInfo, Option<Box<fsys::ResolvedState>>)>,
) -> fsys::RealmQueryProxy {
    let (client, mut stream) = create_proxy_and_stream::<fsys::RealmQueryMarker>().unwrap();
    Task::spawn(async move {
        loop {
            let (moniker, responder) = match stream.next().await.unwrap().unwrap() {
                fsys::RealmQueryRequest::GetInstanceInfo { moniker, responder } => {
                    (moniker, responder)
                }
                _ => panic!("Unexpected RealmQuery request"),
            };
            let response = instances.remove(&moniker);
            match response {
                Some(instance) => responder.send(&mut Ok(instance)).unwrap(),
                None => responder.send(&mut Err(fsys::RealmQueryError::InstanceNotFound)).unwrap(),
            };
        }
    })
    .detach();
    client
}

// Create an arbitrary path string with tmp as the root.
pub fn create_tmp_path() -> String {
    let tmp_dir = tempdir();
    let dir = tmp_dir.as_ref().unwrap();
    let tmp_path = String::from(dir.path().to_str().unwrap());
    tmp_dir.expect("Could not close file").close().unwrap();
    return tmp_path;
}

pub fn serve_realm_query_with_namespace(
    server: ServerEnd<fio::DirectoryMarker>,
    seed_files: HashMap<&'static str, &'static str>,
) -> Result<()> {
    let tmp_path = create_tmp_path();
    let () = create_dir(&tmp_path).unwrap();

    for (new_file, new_file_contents) in seed_files.iter() {
        let new_file_path = format!("{}/{}", tmp_path, new_file);
        write(&new_file_path, new_file_contents).unwrap();
    }

    fuchsia_fs::directory::open_channel_in_namespace(
        &tmp_path,
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE | fio::OpenFlags::DIRECTORY,
        ServerEnd::new(server.into_channel()),
    )
    .unwrap();
    Ok(())
}
