// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{run_events::RunEvent, self_diagnostics},
    anyhow::{Context, Error},
    fidl::endpoints::{create_endpoints, ClientEnd},
    fidl_fuchsia_io as fio, fidl_fuchsia_test_internal as ftest_internal,
    fidl_fuchsia_test_manager as ftest_manager,
    fidl_fuchsia_test_manager::{DebugData, DebugDataIteratorMarker, DebugDataIteratorRequest},
    fuchsia_async as fasync,
    futures::{channel::mpsc, future::join_all, prelude::*, Future, StreamExt, TryStreamExt},
    std::path::{Path, PathBuf},
    tracing::warn,
};

struct DebugDataFile {
    pub name: String,
    pub contents: Vec<u8>,
}

fn serve_debug_data(
    files: Vec<DebugDataFile>,
    serve_root_dir: PathBuf,
) -> (ClientEnd<DebugDataIteratorMarker>, impl 'static + Future<Output = Result<(), Error>>) {
    let read_write_flags: fuchsia_fs::OpenFlags =
        fuchsia_fs::OpenFlags::RIGHT_READABLE | fuchsia_fs::OpenFlags::RIGHT_WRITABLE;
    let overwite_file_flag: fuchsia_fs::OpenFlags = fuchsia_fs::OpenFlags::RIGHT_WRITABLE
        | fuchsia_fs::OpenFlags::RIGHT_READABLE
        | fuchsia_fs::OpenFlags::TRUNCATE
        | fuchsia_fs::OpenFlags::CREATE;

    let (client, server) = fidl::endpoints::create_endpoints::<DebugDataIteratorMarker>().unwrap();

    let fut = async move {
        let (file_sender, mut file_recv) = mpsc::channel(0);

        let copy_to_tmp_task = fasync::Task::spawn(async move {
            let tmp_dir_root = fuchsia_fs::open_directory_in_namespace(
                serve_root_dir.to_str().unwrap(),
                read_write_flags,
            )?;
            let tmp_dir_root_ref = &tmp_dir_root;
            futures::stream::iter(files)
                .map(Ok)
                .try_for_each_concurrent(None, move |DebugDataFile { name, contents }| {
                    let mut sender_clone = file_sender.clone();
                    let rel_file_path = PathBuf::from(name);
                    async move {
                        if let Some(parent) = rel_file_path.parent() {
                            if !parent.as_os_str().is_empty() {
                                fuchsia_fs::create_sub_directories(tmp_dir_root_ref, parent)
                                    .context("create subdirectories")?;
                            }
                        }
                        let file = fuchsia_fs::open_file(
                            tmp_dir_root_ref,
                            &rel_file_path,
                            overwite_file_flag,
                        )
                        .context("open file")?;
                        fuchsia_fs::write_file_bytes(&file, &contents)
                            .await
                            .context("write file")?;
                        let (node_client, node_server) =
                            create_endpoints::<fio::NodeMarker>().context("create node")?;
                        file.clone(fuchsia_fs::OpenFlags::RIGHT_READABLE, node_server)
                            .context("clone file")?;
                        // error sending here is okay, receiver could close if client isn't reading the files.
                        let _ = sender_clone.send((rel_file_path, node_client)).await;
                        Result::<_, Error>::Ok(())
                    }
                })
                .await
        });

        let mut stream = server.into_stream()?;

        while let Ok(Some(req)) = stream.try_next().await {
            match req {
                DebugDataIteratorRequest::GetNext { responder, .. } => {
                    match file_recv.next().await {
                        Some((filename, file_client)) => {
                            let mut data_iter = vec![DebugData {
                                name: Some(filename.to_string_lossy().into_owned()),
                                file: Some(ClientEnd::<fio::FileMarker>::new(
                                    file_client.into_channel(),
                                )),
                                ..DebugData::EMPTY
                            }]
                            .into_iter();

                            let _ = responder.send(&mut data_iter);
                        }
                        None => {
                            let _ = responder.send(&mut vec![].into_iter());
                        }
                    }
                }
            }
        }
        copy_to_tmp_task.await?;
        Ok(())
    };

    (client, fut)
}

pub async fn send_debug_data_if_produced(
    mut event_sender: mpsc::Sender<RunEvent>,
    mut controller_events: ftest_internal::DebugDataSetControllerEventStream,
    debug_iterator: ClientEnd<ftest_manager::DebugDataIteratorMarker>,
    inspect_node: &self_diagnostics::RunInspectNode,
) {
    inspect_node.set_debug_data_state(self_diagnostics::DebugDataState::PendingDebugDataProduced);
    match controller_events.next().await {
        Some(Ok(ftest_internal::DebugDataSetControllerEvent::OnDebugDataProduced {})) => {
            let _ = event_sender.send(RunEvent::debug_data(debug_iterator).into()).await;
            inspect_node.set_debug_data_state(self_diagnostics::DebugDataState::DebugDataProduced);
        }
        Some(Err(_)) | None => {
            inspect_node.set_debug_data_state(self_diagnostics::DebugDataState::NoDebugData);
        }
    }
}

const PROFILE_ARTIFACT_ENUMERATE_TIMEOUT_SECONDS: i64 = 15;
const DYNAMIC_PROFILE_PREFIX: &'static str = "/debugdata/llvm-profile/dynamic";
const STATIC_PROFILE_PREFIX: &'static str = "/debugdata/llvm-profile/static";

pub async fn send_kernel_debug_data(mut event_sender: mpsc::Sender<RunEvent>) {
    let prefixes = vec![DYNAMIC_PROFILE_PREFIX, STATIC_PROFILE_PREFIX];
    let directories = prefixes
        .iter()
        .filter_map(|path| {
            match fuchsia_fs::open_directory_in_namespace(
                path,
                fuchsia_fs::OpenFlags::RIGHT_READABLE,
            ) {
                Ok(d) => Some((*path, d)),
                Err(e) => {
                    warn!("Failed to open {} profile directory: {:?}", path, e);
                    None
                }
            }
        })
        .collect::<Vec<(&str, fio::DirectoryProxy)>>();

    // Iterate over files as tuples containing an entry and the prefix the entry was found at.
    struct IteratedEntry {
        prefix: &'static str,
        entry: fuchsia_fs::directory::DirEntry,
    }

    // Create a single stream over the files in all directories
    let mut file_stream = futures::stream::iter(
        directories
            .iter()
            .map(move |val| {
                let (prefix, directory) = val;
                fuchsia_fs::directory::readdir_recursive(
                    directory,
                    Some(fasync::Duration::from_seconds(
                        PROFILE_ARTIFACT_ENUMERATE_TIMEOUT_SECONDS,
                    )),
                )
                .map_ok(move |file| IteratedEntry { prefix: prefix, entry: file })
            })
            .collect::<Vec<_>>(),
    )
    .flatten();

    let mut file_futs = vec![];
    while let Some(Ok(IteratedEntry { prefix, entry })) = file_stream.next().await {
        file_futs.push(async move {
            let prefix = PathBuf::from(prefix);
            let name = entry.name;
            let path = prefix.join(&name).to_string_lossy().to_string();
            let file =
                fuchsia_fs::file::open_in_namespace(&path, fuchsia_fs::OpenFlags::RIGHT_READABLE)?;
            let content = fuchsia_fs::read_file_bytes(&file).await;

            // Store the file in a directory prefixed with the last part of the file path (i.e.
            // "static" or "dynamic").
            let name_prefix = prefix
                .file_stem()
                .map(|v| v.to_string_lossy().to_string())
                .unwrap_or_else(|| "".to_string());
            let name = format!("{}/{}", name_prefix, name);

            Ok::<_, Error>((name, content))
        });
    }

    let file_futs: Vec<(String, Result<Vec<u8>, Error>)> =
        join_all(file_futs).await.into_iter().filter_map(|v| v.ok()).collect();

    let files = file_futs
        .into_iter()
        .filter_map(|v| {
            let (name, result) = v;
            match result {
                Ok(contents) => Some(DebugDataFile { name: name.to_string(), contents }),
                Err(e) => {
                    warn!("Failed to read debug data file {}: {:?}", name, e);
                    None
                }
            }
        })
        .collect::<Vec<_>>();

    if !files.is_empty() {
        // We copy the files to a well known directory in /tmp. This supports exporting the
        // files off device via SCP. Once this flow is no longer needed, we can use something
        // like an ephemeral directory which is torn down once we're done instead.
        let (client, fut) = serve_debug_data(files, Path::new("/tmp").to_path_buf());
        let task = fasync::Task::spawn(
            fut.unwrap_or_else(|e| warn!("Error serving kernel debug data: {:?}", e)),
        );
        let _ = event_sender.send(RunEvent::debug_data(client).into()).await;
        event_sender.disconnect(); // No need to hold this open while we serve the task.
        task.await;
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fuchsia_async as fasync,
        maplit::hashset,
        std::{collections::HashSet, iter::FromIterator},
        tempfile::tempdir,
    };

    #[fuchsia::test]
    async fn empty_data_returns_empty_repeatedly() {
        let dir = tempdir().unwrap();
        let (client, task) = serve_debug_data(vec![], dir.path().to_path_buf());
        let task = fasync::Task::spawn(task);

        let proxy = client.into_proxy().expect("into proxy");

        let values = proxy.get_next().await.expect("get next");
        assert_eq!(values, vec![]);

        let values = proxy.get_next().await.expect("get next");
        assert_eq!(values, vec![]);

        // Disconnecting stops the serving task.
        std::mem::drop(proxy);
        task.await.unwrap();
    }

    #[fuchsia::test]
    async fn single_response() {
        let dir = tempdir().unwrap();
        let (client, task) = serve_debug_data(
            vec![DebugDataFile { name: "file".to_string(), contents: b"test".to_vec() }],
            dir.path().to_path_buf(),
        );
        let _task = fasync::Task::spawn(task);

        let proxy = client.into_proxy().expect("into proxy");

        let mut values = proxy.get_next().await.expect("get next");
        assert_eq!(1usize, values.len());
        let DebugData { name, file, .. } = values.pop().unwrap();
        assert_eq!(Some("file".to_string()), name);
        let contents = fuchsia_fs::read_file_bytes(&file.expect("has file").into_proxy().unwrap())
            .await
            .expect("read file");
        assert_eq!(b"test".to_vec(), contents);

        let values = proxy.get_next().await.expect("get next");
        assert_eq!(values, vec![]);
    }

    #[fuchsia::test]
    async fn multiple_responses() {
        let dir = tempdir().unwrap();
        let (client, task) = serve_debug_data(
            vec![
                DebugDataFile { name: "file".to_string(), contents: b"test".to_vec() },
                DebugDataFile { name: "file2".to_string(), contents: b"test2".to_vec() },
            ],
            dir.path().to_path_buf(),
        );
        let _task =
            fasync::Task::spawn(task.unwrap_or_else(|e| panic!("Error from server: {:?}", e)));

        let proxy = client.into_proxy().expect("into proxy");

        // Complete all requests for files before reading from files.
        // This test validates that files continue to be served even when a later GetNext() call
        // comes in.
        let mut responses = vec![];
        responses.push(proxy.get_next().await.expect("get next"));
        responses.push(proxy.get_next().await.expect("get next"));
        for response in &responses {
            assert_eq!(1usize, response.len());
        }

        let responses = futures::future::join_all(
            responses
                .into_iter()
                .flatten()
                .map(|response| async move {
                    let _ = &response;
                    let DebugData { name, file, .. } = response;
                    let contents =
                        fuchsia_fs::read_file_bytes(&file.expect("has file").into_proxy().unwrap())
                            .await
                            .expect("read file");
                    (name.expect("has name"), contents)
                })
                .collect::<Vec<_>>(),
        )
        .await;

        assert_eq!(
            HashSet::from_iter(responses),
            hashset![
                ("file".to_string(), b"test".to_vec()),
                ("file2".to_string(), b"test2".to_vec()),
            ]
        );
    }
}
