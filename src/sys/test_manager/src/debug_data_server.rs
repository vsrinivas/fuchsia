// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::run_events::RunEvent,
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::{create_endpoints, create_request_stream, ClientEnd},
    fidl_fuchsia_io as fio, fidl_fuchsia_test_manager as ftest_manager,
    fidl_fuchsia_test_manager::{DebugData, DebugDataIteratorMarker, DebugDataIteratorRequest},
    fuchsia_async as fasync,
    futures::{
        channel::mpsc, pin_mut, prelude::*, stream::FusedStream, Future, StreamExt, TryStreamExt,
    },
    std::path::{Path, PathBuf},
    tracing::warn,
};

struct DebugDataFile {
    pub name: String,
    pub contents: Vec<u8>,
}

fn serve_kernel_debug_data(
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
            let tmp_dir_root = fuchsia_fs::directory::open_in_namespace(
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
                                fuchsia_fs::directory::create_directory_recursive(
                                    tmp_dir_root_ref,
                                    parent.to_str().ok_or(anyhow!("Invalid path"))?,
                                    fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
                                )
                                .await
                                .context("create subdirectories")?;
                            }
                        }
                        let file = fuchsia_fs::open_file(
                            tmp_dir_root_ref,
                            &rel_file_path,
                            overwite_file_flag,
                        )
                        .context("open file")?;
                        fuchsia_fs::file::write(&file, &contents).await.context("write file")?;
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

const DEBUG_DATA_TIMEOUT_SECONDS: i64 = 15;
const DEBUG_DATA_PATH: &'static str = "/debugdata";

// TODO(fxbug.dev/110062): Once scp is no longer needed this can just call serve_iterator instead.
pub(crate) async fn send_kernel_debug_data(mut event_sender: mpsc::Sender<RunEvent>) {
    let root_dir = match fuchsia_fs::directory::open_in_namespace(
        DEBUG_DATA_PATH,
        fuchsia_fs::OpenFlags::RIGHT_READABLE,
    ) {
        Ok(dir) => dir,
        Err(err) => {
            warn!("Failed to open '/debugdata'. Error: {}", err);
            return;
        }
    };

    let files = fuchsia_fs::directory::readdir_recursive(
        &root_dir,
        Some(fasync::Duration::from_seconds(DEBUG_DATA_TIMEOUT_SECONDS)),
    )
    .filter_map(|result| async move {
        match result {
            Ok(entry) => {
                if entry.kind != fio::DirentType::File {
                    None
                } else {
                    Some(entry)
                }
            }
            Err(err) => {
                warn!("Error while reading directory entry. Error: {}", err);
                None
            }
        }
    })
    .filter_map(|entry| async move {
        let path = PathBuf::from(DEBUG_DATA_PATH).join(entry.name).to_string_lossy().to_string();
        match fuchsia_fs::file::open_in_namespace(&path, fuchsia_fs::OpenFlags::RIGHT_READABLE) {
            Ok(file) => Some((path, file)),
            Err(err) => {
                warn!("Failed to read file {}. Error {}", path, err);
                None
            }
        }
    })
    .filter_map(|(path, file)| async move {
        match fuchsia_fs::file::read(&file).await {
            Ok(contents) => Some((path, contents)),
            Err(err) => {
                warn!("Failed to read file {}. Error {}", path, err);
                None
            }
        }
    })
    .map(|(name, contents)| {
        // Remove the leading '/' so there is no 'root' entry.
        DebugDataFile { name: name.trim_start_matches('/').to_string(), contents }
    })
    .collect::<Vec<DebugDataFile>>()
    .await;

    if !files.is_empty() {
        // We copy the files to a well known directory in /tmp. This supports exporting the
        // files off device via SCP. Once this flow is no longer needed, we can use something
        // like an ephemeral directory which is torn down once we're done instead.
        let (client, fut) = serve_kernel_debug_data(files, Path::new("/tmp").to_path_buf());
        let task = fasync::Task::spawn(
            fut.unwrap_or_else(|e| warn!("Error serving kernel debug data: {:?}", e)),
        );
        let _ = event_sender.send(RunEvent::debug_data(client).into()).await;
        event_sender.disconnect(); // No need to hold this open while we serve the task.
        task.await;
    }
}

const ITERATOR_BATCH_SIZE: usize = 10;

async fn filter_map_filename(
    entry_result: Result<fuchsia_fs::directory::DirEntry, fuchsia_fs::directory::Error>,
    dir_path: &str,
) -> Option<String> {
    match entry_result {
        Ok(fuchsia_fs::directory::DirEntry { name, kind }) => match kind {
            fuchsia_fs::directory::DirentKind::File => Some(name),
            _ => None,
        },
        Err(e) => {
            warn!("Error reading directory in {}: {:?}", dir_path, e);
            None
        }
    }
}

/// Serves the |DebugDataIterator| protocol by serving all the files contained under
/// |dir_path|.
///
/// The contents under |dir_path| are assumed to not change while the iterator is served.
pub(crate) async fn serve_iterator(
    dir_path: &str,
    mut event_sender: mpsc::Sender<RunEvent>,
) -> Result<(), Error> {
    let directory =
        fuchsia_fs::directory::open_in_namespace(dir_path, fuchsia_fs::OpenFlags::RIGHT_READABLE)?;
    let file_stream = fuchsia_fs::directory::readdir_recursive(
        &directory,
        Some(fasync::Duration::from_seconds(DEBUG_DATA_TIMEOUT_SECONDS)),
    )
    .filter_map(|entry| filter_map_filename(entry, dir_path))
    .peekable();
    pin_mut!(file_stream);

    if file_stream.as_mut().peek().await.is_none() {
        // No files to serve.
        return Ok(());
    }

    let mut file_stream = file_stream.fuse();

    let (client, mut iterator) = create_request_stream::<ftest_manager::DebugDataIteratorMarker>()?;
    let _ = event_sender.send(RunEvent::debug_data(client).into()).await;
    event_sender.disconnect(); // No need to hold this open while we serve the iterator.

    while let Some(request) = iterator.try_next().await? {
        let ftest_manager::DebugDataIteratorRequest::GetNext { responder } = request;
        let next_files = match file_stream.is_terminated() {
            true => vec![],
            false => file_stream.by_ref().take(ITERATOR_BATCH_SIZE).collect().await,
        };
        let debug_data = next_files
            .into_iter()
            .map(|file_name| {
                let (file, server) = create_endpoints::<fio::NodeMarker>()?;
                directory.open(fuchsia_fs::OpenFlags::RIGHT_READABLE, 0, &file_name, server)?;
                Ok(ftest_manager::DebugData {
                    file: Some(ClientEnd::new(file.into_channel())),
                    name: file_name.into(),
                    ..ftest_manager::DebugData::EMPTY
                })
            })
            .collect::<Result<Vec<_>, Error>>()?;
        let _ = responder.send(&mut debug_data.into_iter());
    }
    Ok(())
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::run_events::RunEventPayload,
        fuchsia_async as fasync,
        maplit::hashset,
        std::{collections::HashSet, iter::FromIterator},
        tempfile::tempdir,
    };

    #[fuchsia::test]
    async fn empty_data_returns_empty_repeatedly() {
        let dir = tempdir().unwrap();
        let (client, task) = serve_kernel_debug_data(vec![], dir.path().to_path_buf());
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
        let (client, task) = serve_kernel_debug_data(
            vec![DebugDataFile { name: "file".to_string(), contents: b"test".to_vec() }],
            dir.path().to_path_buf(),
        );
        let _task = fasync::Task::spawn(task);

        let proxy = client.into_proxy().expect("into proxy");

        let mut values = proxy.get_next().await.expect("get next");
        assert_eq!(1usize, values.len());
        let DebugData { name, file, .. } = values.pop().unwrap();
        assert_eq!(Some("file".to_string()), name);
        let contents = fuchsia_fs::file::read(&file.expect("has file").into_proxy().unwrap())
            .await
            .expect("read file");
        assert_eq!(b"test".to_vec(), contents);

        let values = proxy.get_next().await.expect("get next");
        assert_eq!(values, vec![]);
    }

    #[fuchsia::test]
    async fn multiple_responses() {
        let dir = tempdir().unwrap();
        let (client, task) = serve_kernel_debug_data(
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
                        fuchsia_fs::file::read(&file.expect("has file").into_proxy().unwrap())
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

    async fn serve_iterator_from_tmp(
        dir: &tempfile::TempDir,
    ) -> (Option<ftest_manager::DebugDataIteratorProxy>, fasync::Task<Result<(), Error>>) {
        let (send, mut recv) = mpsc::channel(0);
        let dir_path = dir.path().to_str().unwrap().to_string();
        let task = fasync::Task::local(async move { serve_iterator(&dir_path, send).await });
        let proxy = recv.next().await.map(|event| {
            let RunEventPayload::DebugData(client) = event.into_payload();
            client.into_proxy().expect("into proxy")
        });
        (proxy, task)
    }

    #[fuchsia::test]
    async fn serve_iterator_empty_dir_returns_no_client() {
        let dir = tempdir().unwrap();
        let (client, task) = serve_iterator_from_tmp(&dir).await;
        assert!(client.is_none());
        task.await.expect("iterator server should not fail");
    }

    #[fuchsia::test]
    async fn serve_iterator_single_response() {
        let dir = tempdir().unwrap();
        fuchsia_fs::file::write_in_namespace(&dir.path().join("file").to_string_lossy(), "test")
            .await
            .expect("write to file");

        let (client, task) = serve_iterator_from_tmp(&dir).await;

        let proxy = client.expect("client to be returned");

        let mut values = proxy.get_next().await.expect("get next");
        assert_eq!(1usize, values.len());
        let ftest_manager::DebugData { name, file, .. } = values.pop().unwrap();
        assert_eq!(Some("file".to_string()), name);
        let contents = fuchsia_fs::file::read(&file.expect("has file").into_proxy().unwrap())
            .await
            .expect("read file");
        assert_eq!(b"test".to_vec(), contents);

        let values = proxy.get_next().await.expect("get next");
        assert_eq!(values, vec![]);

        drop(proxy);
        task.await.expect("iterator server should not fail");
    }

    #[fuchsia::test]
    async fn serve_iterator_multiple_responses() {
        let num_files_served = ITERATOR_BATCH_SIZE * 2;

        let dir = tempdir().unwrap();
        for idx in 0..num_files_served {
            fuchsia_fs::file::write_in_namespace(
                &dir.path().join(format!("file-{:?}", idx)).to_string_lossy(),
                &format!("test-{:?}", idx),
            )
            .await
            .expect("write to file");
        }

        let (client, task) = serve_iterator_from_tmp(&dir).await;

        let proxy = client.expect("client to be returned");

        let mut all_files = vec![];
        loop {
            let mut next = proxy.get_next().await.expect("get next");
            if next.is_empty() {
                break;
            }
            all_files.append(&mut next);
        }

        let file_contents: HashSet<_> = futures::stream::iter(all_files)
            .then(|ftest_manager::DebugData { name, file, .. }| async move {
                let contents =
                    fuchsia_fs::read_file(&file.expect("has file").into_proxy().unwrap())
                        .await
                        .expect("read file");
                (name.unwrap(), contents)
            })
            .collect()
            .await;

        let expected_files: HashSet<_> = (0..num_files_served)
            .map(|idx| (format!("file-{:?}", idx), format!("test-{:?}", idx)))
            .collect();

        assert_eq!(file_contents, expected_files);
        drop(proxy);
        task.await.expect("iterator server should not fail");
    }
}
