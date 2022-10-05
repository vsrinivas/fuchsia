// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        artifacts,
        cancel::NamedFutureExt,
        diagnostics::{self, LogCollectionOutcome},
        outcome::RunTestSuiteError,
        outcome::UnexpectedEventError,
        output::{
            ArtifactType, DirectoryArtifactType, DynDirectoryArtifact, DynReporter, EntityReporter,
        },
        stream_util::StreamUtil,
    },
    anyhow::{anyhow, Context as _},
    fidl::Peered,
    fidl_fuchsia_io as fio, fidl_fuchsia_test_manager as ftest_manager, fuchsia_async as fasync,
    futures::{
        future::{join_all, BoxFuture, Future, FutureExt, TryFutureExt},
        stream::{FuturesUnordered, StreamExt, TryStreamExt},
    },
    std::{borrow::Borrow, collections::VecDeque, io::Write, path::PathBuf},
    tracing::{debug, warn},
};

/// Given an |artifact| reported over fuchsia.test.manager, create the appropriate artifact in the
/// reporter. Returns a Future, which when polled to completion, drains the results from |artifact|
/// and saves them to the reporter.
///
/// This method is an async method returning a Future so that the lifetime of |reporter| is not
/// tied to the lifetime of the Future.
/// The returned Future resolves to LogCollectionOutcome when logs are processed.
pub(crate) async fn drain_artifact<'a, E, T, F>(
    reporter: &'a EntityReporter<E, T>,
    artifact: ftest_manager::Artifact,
    log_opts: diagnostics::LogCollectionOptions,
    log_timeout: diagnostics::LogTimeoutOptions<F>,
) -> Result<
    BoxFuture<'static, Result<Option<LogCollectionOutcome>, anyhow::Error>>,
    RunTestSuiteError,
>
where
    T: Borrow<DynReporter>,
    F: Future + Send + 'static,
{
    match artifact {
        ftest_manager::Artifact::Stdout(socket) => {
            let stdout = reporter.new_artifact(&ArtifactType::Stdout).await?;
            Ok(copy_socket_artifact(socket, stdout).map_ok(|()| None).named("stdout").boxed())
        }
        ftest_manager::Artifact::Stderr(socket) => {
            let stderr = reporter.new_artifact(&ArtifactType::Stderr).await?;
            Ok(copy_socket_artifact(socket, stderr).map_ok(|()| None).named("stderr").boxed())
        }
        ftest_manager::Artifact::Log(syslog) => {
            let syslog_artifact = reporter.new_artifact(&ArtifactType::Syslog).await?;
            Ok(diagnostics::collect_logs(
                test_diagnostics::LogStream::from_syslog(syslog)?,
                syslog_artifact,
                log_opts,
                log_timeout,
            )
            .map_ok(Some)
            .named("syslog")
            .boxed())
        }
        ftest_manager::Artifact::Custom(ftest_manager::CustomArtifact {
            directory_and_token,
            component_moniker,
            ..
        }) => {
            let ftest_manager::DirectoryAndToken { directory, token, .. } = directory_and_token
                .ok_or(UnexpectedEventError::MissingRequiredField {
                    containing_struct: "CustomArtifact",
                    field: "directory_and_token",
                })?;
            let directory_artifact = reporter
                .new_directory_artifact(&DirectoryArtifactType::Custom, component_moniker)
                .await?;
            Ok(async move {
                let directory = directory.into_proxy()?;
                let result =
                    artifacts::copy_custom_artifact_directory(directory, directory_artifact).await;
                // TODO(fxbug.dev/84882): Remove this signal once Overnet
                // supports automatically signalling EVENTPAIR_CLOSED when the
                // handle is closed.
                let _ = token.signal_peer(fidl::Signals::empty(), fidl::Signals::USER_0);
                result
            }
            .map_ok(|()| None)
            .named("custom_artifacts")
            .boxed())
        }
        ftest_manager::Artifact::DebugData(iterator) => {
            let output_directory = reporter
                .new_directory_artifact(&DirectoryArtifactType::Debug, None /* moniker */)
                .await?;
            Ok(artifacts::copy_debug_data(iterator.into_proxy()?, output_directory)
                .map(|()| Ok(None))
                .named("debug_data")
                .boxed())
        }
        ftest_manager::ArtifactUnknown!() => {
            warn!("Encountered an unknown artifact");
            Ok(futures::future::ready(Ok(None)).boxed())
        }
    }
}

/// Copy an artifact reported over a socket.
async fn copy_socket_artifact<W: Write>(
    socket: fidl::Socket,
    mut artifact: W,
) -> Result<(), anyhow::Error> {
    let mut async_socket = fidl::AsyncSocket::from_socket(socket)?;
    loop {
        let done =
            test_diagnostics::SocketReadFut::new(&mut async_socket, |maybe_buf| match maybe_buf {
                Some(buf) => {
                    artifact.write_all(buf)?;
                    Ok(false)
                }
                None => Ok(true),
            })
            .await?;
        if done {
            artifact.flush()?;
            return Ok(());
        }
    }
}

/// Copy debug data reported over a debug data iterator to an output directory.
async fn copy_debug_data(
    iterator: ftest_manager::DebugDataIteratorProxy,
    output_directory: Box<DynDirectoryArtifact>,
) {
    const PIPELINED_REQUESTS: usize = 4;
    let unprocessed_data_stream =
        futures::stream::repeat_with(move || iterator.get_next()).buffered(PIPELINED_REQUESTS);
    let terminated_event_stream =
        unprocessed_data_stream.take_until_stop_after(|result| match &result {
            Ok(events) => events.is_empty(),
            _ => true,
        });

    let data_futs = terminated_event_stream
        .map(|result| match result {
            Ok(vals) => vals,
            Err(e) => {
                warn!("Request failure: {:?}", e);
                vec![]
            }
        })
        .map(futures::stream::iter)
        .flatten()
        .map(|debug_data| {
            let output =
                debug_data.name.as_ref().ok_or_else(|| anyhow!("Missing profile name")).and_then(
                    |name| {
                        output_directory.new_file(&PathBuf::from(name)).map_err(anyhow::Error::from)
                    },
                );
            fasync::Task::spawn(async move {
                let _ = &debug_data;
                let mut output = output?;
                let file = debug_data
                    .file
                    .ok_or_else(|| anyhow!("Missing profile file handle"))?
                    .into_proxy()?;
                debug!("Reading run profile \"{:?}\"", debug_data.name);
                copy_file_to_writer(&file, &mut output).await
            })
        })
        .collect::<Vec<_>>()
        .await;
    join_all(data_futs).await;
    debug!("All profiles downloaded");
}

/// Copy a directory into a directory artifact.
async fn copy_custom_artifact_directory(
    directory: fio::DirectoryProxy,
    out_dir: Box<DynDirectoryArtifact>,
) -> Result<(), anyhow::Error> {
    let mut paths = vec![];
    let mut enumerate = fuchsia_fs::directory::readdir_recursive(&directory, None);
    while let Ok(Some(file)) = enumerate.try_next().await {
        if file.kind == fuchsia_fs::directory::DirentKind::File {
            paths.push(file.name);
        }
    }

    let futs = FuturesUnordered::new();
    paths.iter().for_each(|path| {
        let path = std::path::PathBuf::from(path);
        let file = fuchsia_fs::open_file(&directory, &path, fuchsia_fs::OpenFlags::RIGHT_READABLE);
        let output_file = out_dir.new_file(&path);
        futs.push(async move {
            let file = file.with_context(|| format!("with path {:?}", path))?;
            let mut output_file = output_file?;
            copy_file_to_writer(&file, &mut output_file).await
        });
    });

    futs.for_each(|result| {
        if let Err(e) = result {
            warn!("Custom artifact failure: {}", e);
        }
        async move {}
    })
    .await;

    Ok(())
}

async fn copy_file_to_writer<T: Write>(
    file: &fio::FileProxy,
    output: &mut T,
) -> Result<(), anyhow::Error> {
    const READ_SIZE: u64 = fio::MAX_BUF;

    let mut vector = VecDeque::new();
    // Arbitrary number of reads to pipeline.
    const PIPELINED_READ_COUNT: u64 = 4;
    for _n in 0..PIPELINED_READ_COUNT {
        vector.push_back(file.read(READ_SIZE));
    }
    loop {
        let mut buf =
            vector.pop_front().unwrap().await?.map_err(fuchsia_zircon_status::Status::from_raw)?;
        if buf.is_empty() {
            break;
        }
        output.write_all(&mut buf)?;
        vector.push_back(file.read(READ_SIZE));
    }
    Ok(())
}

#[cfg(test)]
mod socket_tests {
    use {super::*, futures::AsyncWriteExt};

    #[fuchsia::test]
    async fn copy_socket() {
        let cases = vec![vec![], b"0123456789abcde".to_vec(), vec![0u8; 4096]];

        for case in cases.iter() {
            let (client_socket, server_socket) =
                fidl::Socket::create(fidl::SocketOpts::STREAM).expect("create socket");
            let mut output = vec![];
            let write_fut = async move {
                let mut async_socket =
                    fidl::AsyncSocket::from_socket(server_socket).expect("create socket");
                async_socket.write_all(case.as_slice()).await.expect("write bytes");
            };

            let ((), res) =
                futures::future::join(write_fut, copy_socket_artifact(client_socket, &mut output))
                    .await;
            res.expect("copy contents");
            assert_eq!(output.as_slice(), case.as_slice());
        }
    }
}

// These tests use vfs, which is only available on Fuchsia.
#[cfg(target_os = "fuchsia")]
#[cfg(test)]
mod file_tests {
    use {
        super::*,
        crate::output::InMemoryDirectoryWriter,
        fidl::endpoints::{ClientEnd, Proxy, ServerEnd},
        fidl_fuchsia_io as fio,
        maplit::hashmap,
        std::{collections::HashMap, sync::Arc},
        vfs::{
            directory::{entry::DirectoryEntry, helper::DirectlyMutable, immutable::Simple},
            execution_scope::ExecutionScope,
            file::vmo::read_only_static,
            pseudo_directory,
        },
    };

    async fn serve_and_copy_debug_data(
        fake_dir: Arc<Simple>,
        directory_writer: InMemoryDirectoryWriter,
    ) {
        let (directory_client, directory_service) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        fake_dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            ServerEnd::new(directory_service.into_channel()),
        );
        let mut paths = vec![];
        let mut enumerate = fuchsia_fs::directory::readdir_recursive(&directory_client, None);
        while let Ok(Some(file)) = enumerate.try_next().await {
            if file.kind == fuchsia_fs::directory::DirentKind::File {
                paths.push(file.name);
            }
        }
        let mut served_files = vec![];
        paths.iter().for_each(|path| {
            let file = fuchsia_fs::open_file(
                &directory_client,
                &std::path::PathBuf::from(&path),
                fuchsia_fs::OpenFlags::RIGHT_READABLE,
            )
            .expect("open file");
            served_files.push(ftest_manager::DebugData {
                name: Some(path.to_string()),
                file: Some(ClientEnd::new(file.into_channel().unwrap().into_zx_channel())),
                ..ftest_manager::DebugData::EMPTY
            });
        });

        let (iterator_proxy, mut iterator_stream) =
            fidl::endpoints::create_proxy_and_stream::<ftest_manager::DebugDataIteratorMarker>()
                .unwrap();
        let serve_fut = async move {
            let mut files_iter = served_files.into_iter();
            while let Ok(Some(request)) = iterator_stream.try_next().await {
                let ftest_manager::DebugDataIteratorRequest::GetNext { responder } = request;
                let resp: Vec<_> = files_iter.by_ref().take(3).collect();
                let _ = responder.send(&mut resp.into_iter());
            }
        };
        futures::future::join(
            serve_fut,
            copy_debug_data(iterator_proxy, Box::new(directory_writer)),
        )
        .await;
    }

    async fn serve_and_copy_directory(
        fake_dir: Arc<Simple>,
        directory_writer: InMemoryDirectoryWriter,
    ) {
        let (directory_client, directory_service) =
            fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        fake_dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            vfs::path::Path::dot(),
            ServerEnd::new(directory_service.into_channel()),
        );
        copy_custom_artifact_directory(directory_client, Box::new(directory_writer))
            .await
            .expect("reading custom directory");
    }

    fn test_cases() -> Vec<(&'static str, Arc<Simple>, HashMap<PathBuf, Vec<u8>>)> {
        vec![
            ("empty", pseudo_directory! {}, hashmap! {}),
            (
                "single file",
                pseudo_directory! {
                    "test_file.txt" => read_only_static("Hello, World!"),
                },
                hashmap! {
                    "test_file.txt".to_string().into() => b"Hello, World!".to_vec()
                },
            ),
            (
                "subdir",
                pseudo_directory! {
                    "sub" => pseudo_directory! {
                        "nested.txt" => read_only_static("Nested file!"),
                    }
                },
                hashmap! {
                    "sub/nested.txt".to_string().into() => b"Nested file!".to_vec()
                },
            ),
            (
                "empty file",
                pseudo_directory! {
                    "empty.txt" => read_only_static(""),
                },
                hashmap! {
                    "empty.txt".to_string().into() => b"".to_vec()
                },
            ),
            (
                "big file",
                pseudo_directory! {
                    "big.txt" => read_only_static(vec![b's'; (fio::MAX_BUF as usize)*2]),
                },
                hashmap! {
                    "big.txt".to_string().into() => vec![b's'; (fio::MAX_BUF as usize) *2 as usize]
                },
            ),
            (
                "100 files",
                {
                    let dir = pseudo_directory! {};
                    for i in 0..100 {
                        dir.add_entry(
                            format!("{:?}.txt", i),
                            read_only_static(format!("contents for {:?}", i)),
                        )
                        .expect("add file");
                    }
                    dir
                },
                (0..100)
                    .map(|i| {
                        (
                            format!("{:?}.txt", i).into(),
                            format!("contents for {:?}", i).into_bytes(),
                        )
                    })
                    .collect(),
            ),
        ]
    }

    #[fuchsia::test]
    async fn test_copy_dir() {
        for (name, fake_dir, expected_files) in test_cases() {
            let artifact = InMemoryDirectoryWriter::default();
            serve_and_copy_directory(fake_dir, artifact.clone()).await;
            let actual_files: HashMap<_, _> = artifact
                .files
                .lock()
                .iter()
                .map(|(path, artifact)| (path.clone(), artifact.get_contents()))
                .collect();
            assert_eq!(expected_files, actual_files, "{}", name);
        }
    }

    #[fuchsia::test]
    async fn test_copy_debug_data() {
        for (name, fake_dir, expected_files) in test_cases() {
            let artifact = InMemoryDirectoryWriter::default();
            serve_and_copy_debug_data(fake_dir, artifact.clone()).await;
            let actual_files: HashMap<_, _> = artifact
                .files
                .lock()
                .iter()
                .map(|(path, artifact)| (path.clone(), artifact.get_contents()))
                .collect();
            assert_eq!(expected_files, actual_files, "{}", name);
        }
    }
}
