// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ClientEnd;
use fidl_fuchsia_test_manager::{DebugData, DebugDataIteratorMarker, DebugDataIteratorRequest};
use fuchsia_async as fasync;
use futures::TryStreamExt;
use std::collections::VecDeque;
use vfs::{
    directory::entry::DirectoryEntry, execution_scope::ExecutionScope, file::vmo::read_only_const,
};

pub struct DebugDataFile {
    pub name: String,
    pub contents: Vec<u8>,
}

pub fn serve_debug_data(
    files: Vec<DebugDataFile>,
) -> (ClientEnd<DebugDataIteratorMarker>, fasync::Task<()>) {
    let (client, server) = fidl::endpoints::create_endpoints::<DebugDataIteratorMarker>().unwrap();
    let mut files = files.into_iter().collect::<VecDeque<_>>();

    let task = fasync::Task::spawn(async move {
        let scope = ExecutionScope::new();

        let mut stream = server.into_stream().unwrap();

        while let Ok(Some(req)) = stream.try_next().await {
            match req {
                DebugDataIteratorRequest::GetNext { responder, .. } => {
                    let DebugDataFile { name, contents } = match files.pop_front() {
                        Some(value) => value,
                        None => {
                            // Everything has been send. Return empty batch in the iterator.
                            let _ = responder.send(&mut vec![].into_iter());
                            continue;
                        }
                    };

                    let (file_client, file_server) =
                        fidl::endpoints::create_endpoints::<fidl_fuchsia_io::NodeMarker>().unwrap();

                    let file_impl = read_only_const(&contents);
                    std::mem::drop(contents); // contents are copied; release unneeded memory

                    file_impl.open(
                        scope.clone(),
                        fidl_fuchsia_io::OPEN_RIGHT_READABLE,
                        0,
                        vfs::path::Path::dot(),
                        file_server,
                    );

                    let mut data_iter = vec![DebugData {
                        name: Some(name),
                        file: Some(fidl::endpoints::ClientEnd::<fidl_fuchsia_io::FileMarker>::new(
                            file_client.into_channel(),
                        )),
                        ..DebugData::EMPTY
                    }]
                    .into_iter();

                    let _ = responder.send(&mut data_iter);
                }
            }
        }
    });

    (client, task)
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia::test]
    async fn empty_data_returns_empty_repeatedly() {
        let (client, task) = serve_debug_data(vec![]);

        let proxy = client.into_proxy().expect("into proxy");

        let values = proxy.get_next().await.expect("get next");
        assert_eq!(values, vec![]);

        let values = proxy.get_next().await.expect("get next");
        assert_eq!(values, vec![]);

        // Disconnecting stops the serving task.
        std::mem::drop(proxy);
        task.await;
    }

    #[fuchsia::test]
    async fn single_response() {
        let (client, _task) = serve_debug_data(vec![DebugDataFile {
            name: "file".to_string(),
            contents: b"test".to_vec(),
        }]);

        let proxy = client.into_proxy().expect("into proxy");

        let mut values = proxy.get_next().await.expect("get next");
        assert_eq!(1usize, values.len());
        let DebugData { name, file, .. } = values.pop().unwrap();
        assert_eq!(Some("file".to_string()), name);
        let contents = io_util::read_file_bytes(&file.expect("has file").into_proxy().unwrap())
            .await
            .expect("read file");
        assert_eq!(b"test".to_vec(), contents);

        let values = proxy.get_next().await.expect("get next");
        assert_eq!(values, vec![]);
    }

    #[fuchsia::test]
    async fn multiple_responses() {
        let (client, _task) = serve_debug_data(vec![
            DebugDataFile { name: "file".to_string(), contents: b"test".to_vec() },
            DebugDataFile { name: "file2".to_string(), contents: b"test2".to_vec() },
        ]);

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
                    let DebugData { name, file, .. } = response;
                    let contents =
                        io_util::read_file_bytes(&file.expect("has file").into_proxy().unwrap())
                            .await
                            .expect("read file");
                    (name.expect("has name"), contents)
                })
                .collect::<Vec<_>>(),
        )
        .await;

        assert_eq!(
            responses,
            vec![("file".to_string(), b"test".to_vec()), ("file2".to_string(), b"test2".to_vec()),]
        );
    }
}
