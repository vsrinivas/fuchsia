// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_io as fio;
use fidl_fuchsia_test_manager as ftest_manager;
use futures::stream::{FusedStream, StreamExt, TryStreamExt};
use log::warn;

const ITERATOR_BATCH_SIZE: usize = 10;

/// Serves the |DebugDataIterator| protocol by serving all the files contained under
/// |dir_path|.
///
/// The contents under |dir_path| are assumed to not change while the iterator is served.
pub async fn serve_iterator(
    dir_path: &str,
    mut iterator: ftest_manager::DebugDataIteratorRequestStream,
) -> Result<(), Error> {
    let directory = io_util::open_directory_in_namespace(dir_path, io_util::OPEN_RIGHT_READABLE)?;
    let mut file_stream = files_async::readdir_recursive(&directory, None)
        .filter_map(|entry_result| {
            let result = match entry_result {
                Ok(files_async::DirEntry { name, kind }) => match kind {
                    files_async::DirentKind::File => Some(name),
                    _ => None,
                },
                Err(e) => {
                    warn!("Error reading directory in {}: {:?}", dir_path, e);
                    None
                }
            };
            futures::future::ready(result)
        })
        .fuse();

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
                directory.open(io_util::OPEN_RIGHT_READABLE, 0, &file_name, server)?;
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
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use futures::future::TryFutureExt;
    use std::{
        collections::HashMap,
        fs::{DirBuilder, File},
        io::Write,
    };
    use tempfile::tempdir;

    async fn iterator_test<F, Fut>(dir: &tempfile::TempDir, test_fn: F)
    where
        F: FnOnce(ftest_manager::DebugDataIteratorProxy) -> Fut,
        Fut: futures::Future<Output = ()>,
    {
        let (proxy, stream) =
            create_proxy_and_stream::<ftest_manager::DebugDataIteratorMarker>().unwrap();
        futures::future::join(
            serve_iterator(dir.path().as_os_str().to_str().unwrap(), stream)
                .unwrap_or_else(|e| panic!("Iterator server failed: {:?}", e)),
            test_fn(proxy),
        )
        .await;
    }

    #[fuchsia::test]
    async fn serve_empty_dir() {
        let dir = tempdir().unwrap();
        iterator_test(&dir, |proxy| async move {
            let results = proxy.get_next().await.expect("get next");
            assert!(results.is_empty());
        })
        .await;
    }

    #[fuchsia::test]
    async fn serve_single_file_dir() {
        let dir = tempdir().unwrap();
        let mut f = File::create(dir.path().join("test-file")).expect("create file");
        writeln!(f, "test file content").expect("write to test file");
        drop(f);

        iterator_test(&dir, |proxy| async move {
            let results = proxy.get_next().await.expect("get next");
            assert_eq!(results.len(), 1);
            let result = results.into_iter().next().unwrap();
            assert_eq!(result.name.unwrap(), "test-file");
            let file_proxy = result.file.unwrap().into_proxy().expect("create proxy");
            assert_eq!(
                io_util::read_file(&file_proxy).await.expect("read file"),
                "test file content\n"
            );
        })
        .await;
    }

    #[fuchsia::test]
    async fn serve_single_nested_file_dir() {
        let dir = tempdir().unwrap();
        DirBuilder::new().create(dir.path().join("subdir")).expect("create dir");
        let mut f = File::create(dir.path().join("subdir").join("test-file")).expect("create file");
        writeln!(f, "test file content").expect("write to test file");
        drop(f);

        iterator_test(&dir, |proxy| async move {
            let results = proxy.get_next().await.expect("get next");
            assert_eq!(results.len(), 1);
            let result = results.into_iter().next().unwrap();
            assert_eq!(result.name.unwrap(), "subdir/test-file");
            let file_proxy = result.file.unwrap().into_proxy().expect("create proxy");
            assert_eq!(
                io_util::read_file(&file_proxy).await.expect("read file"),
                "test file content\n"
            );
        })
        .await;
    }

    #[fuchsia::test]
    async fn serve_multiple() {
        let dir = tempdir().unwrap();
        DirBuilder::new().create(dir.path().join("subdir")).expect("create dir");

        let mut expected_files = HashMap::new();
        for i in 0..ITERATOR_BATCH_SIZE + 1 {
            let file_name = format!("test-file-{:?}", i);
            let contents = format!("test file {:?} content\n", i);
            let mut f = File::create(dir.path().join(&file_name)).expect("create file");
            write!(f, "{}", &contents).expect("write to test file");
            expected_files.insert(file_name, contents);
        }
        for i in 0..ITERATOR_BATCH_SIZE + 1 {
            let file_name = format!("test-file-{:?}", i);
            let contents = format!("test subdir file {:?} content\n", i);
            let mut f =
                File::create(dir.path().join("subdir").join(&file_name)).expect("create file");
            write!(f, "{}", &contents).expect("write to test file");
            expected_files.insert(format!("subdir/{}", file_name), contents);
        }

        iterator_test(&dir, move |proxy| async move {
            let mut raw_results = vec![];
            loop {
                let next = proxy.get_next().await.expect("get next");
                if next.is_empty() {
                    break;
                } else {
                    raw_results.extend(next);
                }
            }

            let actual_file_and_contents: HashMap<_, _> = futures::stream::iter(raw_results)
                .then(|debug_data| async move {
                    let contents =
                        io_util::read_file(&debug_data.file.unwrap().into_proxy().unwrap())
                            .await
                            .expect("read file");
                    (debug_data.name.unwrap(), contents)
                })
                .collect()
                .await;

            assert_eq!(expected_files, actual_file_and_contents);
        })
        .await;
    }
}
