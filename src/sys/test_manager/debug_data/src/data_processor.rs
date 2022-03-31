// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::message::VmoMessage;
use anyhow::Error;
use fidl::endpoints::{ClientEnd, Proxy};
use fidl_fuchsia_io as fio;
use fidl_fuchsia_test_debug as ftest_debug;
use futures::stream::{Stream, StreamExt};

/// Max VMOs to send at once. This is limited primarily by the max handles in a channel
/// write, although we may encounter the max byte limit too.
/// TODO(satsukiu): Use tape measure instead
const VMO_CHUNK_SIZE: usize = 32;

/// Processes a stream of |DebugData| VMOs and places the results in |dir_path|.
pub async fn process_debug_data_vmos<S: Stream<Item = VmoMessage> + std::marker::Unpin>(
    dir_path: &str,
    processor_proxy: ftest_debug::DebugDataProcessorProxy,
    event_receiver: S,
) -> Result<(), Error> {
    let directory_proxy = io_util::open_directory_in_namespace(
        dir_path,
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE,
    )?;
    let client_end = ClientEnd::new(directory_proxy.into_channel().unwrap().into_zx_channel());
    processor_proxy.set_directory(client_end)?;

    let mut chunked_events = event_receiver
        .map(|VmoMessage { test_url, data_sink, vmo }| ftest_debug::DebugVmo {
            test_url,
            data_sink,
            vmo,
        })
        .ready_chunks(VMO_CHUNK_SIZE);

    while let Some(mut items) = chunked_events.next().await {
        processor_proxy.add_debug_vmos(&mut items.iter_mut()).await?;
    }
    processor_proxy.finish().await?;
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_io as fio;
    use fuchsia_zircon as zx;
    use futures::stream::TryStreamExt;
    use tempfile::tempdir;

    struct ExpectedVmoMetadata {
        test_url: String,
        data_sink: String,
    }

    impl ExpectedVmoMetadata {
        #[allow(unused)]
        fn new<S: Into<String>, T: Into<String>>(test_url: S, data_sink: T) -> Self {
            Self { test_url: test_url.into(), data_sink: data_sink.into() }
        }
    }

    async fn run_test_processor(
        mut stream: ftest_debug::DebugDataProcessorRequestStream,
        expected_vmos: Vec<ExpectedVmoMetadata>,
    ) {
        let req = stream.try_next().await.expect("get first request").unwrap();
        let dir = match req {
            ftest_debug::DebugDataProcessorRequest::SetDirectory { directory, .. } => {
                directory.into_proxy().expect("convert to proxy")
            }
            other => panic!("First request should be SetDirectory but got {:?}", other),
        };

        // check dir is writable
        let file = io_util::open_file(
            &dir,
            "file".as_ref(),
            fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE,
        )
        .expect("create file");
        io_util::write_file(&file, "file content").await.expect("write to file");
        drop(file);

        let mut collected_vmos = vec![];
        let mut finish_called = false;
        while let Some(req) = stream.try_next().await.expect("get request") {
            match req {
                ftest_debug::DebugDataProcessorRequest::SetDirectory { .. } => {
                    panic!("Set directory called twice")
                }
                ftest_debug::DebugDataProcessorRequest::AddDebugVmos {
                    mut vmos,
                    responder,
                    ..
                } => {
                    collected_vmos.append(&mut vmos);
                    responder.send().unwrap();
                }
                ftest_debug::DebugDataProcessorRequest::Finish { responder, .. } => {
                    // check dir is still writable (resources have not been torn down prematurely).
                    let file = io_util::open_file(
                        &dir,
                        "file_2".as_ref(),
                        fio::OPEN_RIGHT_WRITABLE | fio::OPEN_FLAG_CREATE,
                    )
                    .expect("create file");
                    io_util::write_file(&file, "file content").await.expect("write to file");
                    drop(file);

                    finish_called = true;
                    responder.send().unwrap();
                    break;
                }
            }
        }

        assert!(finish_called);
        let expected_and_actual_iter = expected_vmos.into_iter().zip(collected_vmos.into_iter());
        for (item_no, (expected, actual)) in expected_and_actual_iter.enumerate() {
            assert_eq!(
                expected.test_url, actual.test_url,
                "Test URL mismatch in VMO #{:?}",
                item_no
            );
            assert_eq!(
                expected.data_sink, actual.data_sink,
                "Data sink mismatch in VMO #{:?}",
                item_no
            );
        }
    }

    #[fuchsia::test]
    async fn process_empty_stream() {
        let dir = tempdir().unwrap();
        let (proxy, stream) =
            create_proxy_and_stream::<ftest_debug::DebugDataProcessorMarker>().unwrap();
        let process_fut =
            process_debug_data_vmos(dir.path().to_str().unwrap(), proxy, futures::stream::iter([]));
        let test_processor_fut = run_test_processor(stream, vec![]);
        let (result, ()) = futures::future::join(process_fut, test_processor_fut).await;
        result.expect("processor failed");
    }

    #[fuchsia::test]
    async fn process_single_item_stream() {
        let dir = tempdir().unwrap();
        let (proxy, stream) =
            create_proxy_and_stream::<ftest_debug::DebugDataProcessorMarker>().unwrap();
        let process_fut = process_debug_data_vmos(
            dir.path().to_str().unwrap(),
            proxy,
            futures::stream::iter([VmoMessage {
                test_url: "test_url".to_string(),
                data_sink: "data_sink".to_string(),
                vmo: zx::Vmo::create(1024).unwrap(),
            }]),
        );
        let test_processor_fut =
            run_test_processor(stream, vec![ExpectedVmoMetadata::new("test_url", "data_sink")]);
        let (result, ()) = futures::future::join(process_fut, test_processor_fut).await;
        result.expect("processor failed");
    }

    #[fuchsia::test]
    async fn process_multiple_item_stream() {
        let dir = tempdir().unwrap();
        let (proxy, stream) =
            create_proxy_and_stream::<ftest_debug::DebugDataProcessorMarker>().unwrap();

        let vmo_stream = (0..100).map(|idx| VmoMessage {
            test_url: format!("test_url_{:?}", idx),
            data_sink: format!("data_sink_{:?}", idx),
            vmo: zx::Vmo::create(1024).unwrap(),
        });
        let expected_vmos = (0..100)
            .map(|idx| {
                ExpectedVmoMetadata::new(
                    format!("test_url_{:?}", idx),
                    format!("data_sink_{:?}", idx),
                )
            })
            .collect();

        let process_fut = process_debug_data_vmos(
            dir.path().to_str().unwrap(),
            proxy,
            futures::stream::iter(vmo_stream),
        );
        let test_processor_fut = run_test_processor(stream, expected_vmos);
        let (result, ()) = futures::future::join(process_fut, test_processor_fut).await;
        result.expect("processor failed");
    }
}
