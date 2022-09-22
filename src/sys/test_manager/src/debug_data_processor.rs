// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{debug_data_server, run_events::RunEvent},
    anyhow::Error,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_debugdata as fdebug, fidl_fuchsia_io as fio,
    fidl_fuchsia_test_debug as ftest_debug, fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_component_test::LocalComponentHandles,
    fuchsia_fs::{directory::open_channel_in_namespace, OpenFlags},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        future::FutureExt,
        pin_mut, select_biased,
        stream::{FuturesUnordered, StreamExt, TryStreamExt},
        SinkExt,
    },
};

/// Processor that collects debug data and serves the iterator sending data back to a test
/// executor.
pub(crate) struct DebugDataProcessor {
    directory: DebugDataDirectory,
    receiver: mpsc::Receiver<ftest_debug::DebugVmo>,
    proxy_init_fn: Box<dyn FnOnce() -> Result<ftest_debug::DebugDataProcessorProxy, Error>>,
}

/// Sender used to pass VMOs back to |DebugDataProcessor|.
#[derive(Clone)]
pub(crate) struct DebugDataSender {
    sender: mpsc::Sender<ftest_debug::DebugVmo>,
}

/// Directory used to store collected debug data.
#[derive(Debug)]
#[allow(unused)]
pub enum DebugDataDirectory {
    /// An isolated directory is owned purely by the |DebugDataProcessor| it is given to, and will
    /// be torn down when the |DebugDataProcessor| is terminated.
    Isolated { parent: &'static str },
    /// An accumulated directory may be shared between multiple |DebugDataProcessor|s. Contents
    /// will not be torn down.
    Accumulating { dir: &'static str },
}

impl DebugDataProcessor {
    const MAX_SENT_VMOS: usize = 10;
    /// Create a new |DebugDataProcessor| for processing VMOs, and |DebugDataSender| for passing
    /// it VMOs.
    #[allow(unused)]
    pub fn new(directory: DebugDataDirectory) -> (Self, DebugDataSender) {
        let (sender, receiver) = futures::channel::mpsc::channel(Self::MAX_SENT_VMOS);
        (
            Self {
                directory,
                receiver,
                proxy_init_fn: Box::new(|| {
                    connect_to_protocol::<ftest_debug::DebugDataProcessorMarker>()
                        .map_err(Error::from)
                }),
            },
            DebugDataSender { sender },
        )
    }

    /// Create a new |DebugDataProcessor| for processing VMOs, |DebugDataSender| for passing
    /// it VMOs, and the |fuchsia.test.debug.DebugDataProcessor| stream to which the processor
    /// will connect.
    #[cfg(test)]
    fn new_for_test(directory: DebugDataDirectory) -> DebugDataForTestResult {
        let (sender, receiver) = futures::channel::mpsc::channel(Self::MAX_SENT_VMOS);
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<ftest_debug::DebugDataProcessorMarker>()
                .expect("create stream");
        let maybe_proxy = std::sync::Mutex::new(Some(proxy));
        DebugDataForTestResult {
            processor: Self {
                directory,
                receiver,
                proxy_init_fn: Box::new(move || Ok(maybe_proxy.lock().unwrap().take().unwrap())),
            },
            sender: DebugDataSender { sender },
            stream,
        }
    }

    /// Collect debug data produced by the corresponding |DebugDataSender|, and serve the resulting
    /// data. In case debug data is produced, sends the event over |run_event_sender|.
    #[allow(unused)]
    pub async fn collect_and_serve(
        self,
        run_event_sender: mpsc::Sender<RunEvent>,
    ) -> Result<(), Error> {
        let Self { directory, receiver, proxy_init_fn } = self;

        // Avoid setting up resrources in the common case where no debug data is produced.
        let peekable_reciever = receiver.ready_chunks(Self::MAX_SENT_VMOS).peekable();
        pin_mut!(peekable_reciever);
        if peekable_reciever.as_mut().peek().await.is_none() {
            return Ok(());
        }

        enum MaybeOwnedDirectory {
            Owned(tempfile::TempDir),
            Unowned(&'static str),
        }
        let debug_directory = match directory {
            DebugDataDirectory::Isolated { parent } => {
                MaybeOwnedDirectory::Owned(tempfile::TempDir::new_in(parent)?)
            }
            DebugDataDirectory::Accumulating { dir } => MaybeOwnedDirectory::Unowned(dir),
        };
        let debug_directory_path = match &debug_directory {
            MaybeOwnedDirectory::Owned(tmp) => tmp.path().to_string_lossy(),
            MaybeOwnedDirectory::Unowned(dir) => std::borrow::Cow::Borrowed(*dir),
        };

        let (directory_proxy, server_end) = create_endpoints::<fio::DirectoryMarker>()?;
        open_channel_in_namespace(
            &debug_directory_path,
            OpenFlags::RIGHT_READABLE | OpenFlags::RIGHT_WRITABLE,
            server_end,
        )?;

        let proxy = proxy_init_fn()?;
        proxy.set_directory(directory_proxy)?;
        while let Some(mut chunk) = peekable_reciever.next().await {
            proxy.add_debug_vmos(&mut chunk.iter_mut()).await?;
        }
        proxy.finish().await?;

        debug_data_server::serve_iterator(&debug_directory_path, run_event_sender).await?;

        if let MaybeOwnedDirectory::Owned(tmp) = debug_directory {
            tmp.close()?;
        }
        Ok(())
    }
}

#[cfg(test)]
struct DebugDataForTestResult {
    processor: DebugDataProcessor,
    sender: DebugDataSender,
    stream: ftest_debug::DebugDataProcessorRequestStream,
}

/// Serve |fuchsia.debugdata.Publisher| as a RealmBuilder mock. Collected VMOs are sent over
/// |debug_data_sender| for processing.
#[allow(unused)]
pub(crate) async fn serve_debug_data_publisher(
    handles: LocalComponentHandles,
    test_url: String,
    debug_data_sender: DebugDataSender,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    // Register a notifier so that this mock isn't immediately killed - it needs to drain
    // debug data.
    let stop_recv = handles.register_stop_notifier().await;

    fs.dir("svc").add_fidl_service(|stream: fdebug::PublisherRequestStream| stream);
    fs.serve_connection(handles.outgoing_dir)?;

    let mut drain_tasks = FuturesUnordered::new();
    drain_tasks.push(fasync::Task::spawn(async move {
        let _ = stop_recv.await;
        Ok(())
    }));

    loop {
        select_biased! {
            maybe_stream = fs.next().fuse() => match maybe_stream {
                None => {
                    return drain_tasks.try_collect::<()>().await;
                },
                Some(stream) => {
                    let sender_clone = debug_data_sender.clone();
                    let url_clone = test_url.clone();
                    drain_tasks.push(fasync::Task::spawn(async move {
                        serve_publisher(stream, &url_clone, sender_clone).await?;
                        Ok(())
                    }));
                }
            },
            // Poll for completion of both stop_recv and any futures serving the publisher
            // together. This allows us to accept any new serve requests even if stop is
            // called, so long as at least one other request is still being served.
            maybe_result = drain_tasks.next().fuse() => match maybe_result {
                Some(result) => {
                    result?;
                },
                None => return Ok(()),
            },
        };
    }
}

async fn serve_publisher(
    stream: fdebug::PublisherRequestStream,
    test_url: &str,
    debug_data_sender: DebugDataSender,
) -> Result<(), Error> {
    stream
        .map(Ok)
        .try_for_each_concurrent(None, |req| {
            let test_url = test_url.to_string();
            let mut sender_clone = debug_data_sender.clone();
            async move {
                let fdebug::PublisherRequest::Publish { data_sink, data, vmo_token, .. } = req?;
                // Wait for the token handle to close before sending the VMO for processing.
                // This allows the client to continue modifying the VMO after it has sent it.
                // See |fuchsia.debugdata.Publisher| protocol for details.
                fasync::OnSignals::new(&vmo_token, zx::Signals::EVENTPAIR_CLOSED).await?;
                let _ = sender_clone
                    .sender
                    .send(ftest_debug::DebugVmo { test_url, data_sink, vmo: data })
                    .await;
                Ok(())
            }
        })
        .await
}

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::{run_events::RunEventPayload, utilities::stream_fn},
        fidl::endpoints::create_proxy_and_stream,
        fuchsia_component_test::{
            Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
        },
        futures::{FutureExt, TryFutureExt},
        maplit::hashset,
        std::{collections::HashSet, task::Poll},
    };

    const VMO_SIZE: u64 = 4096;

    /// Runs a fake test processor implementation that, for each VMO received, creates
    /// a new file called "data_sink" and writes the test_url inside it.
    /// |debug_vmo_recevied_sender| is a synchronization hack that sends one message for
    /// each vmo received. It is a workaround for the
    /// Started/Destroyed/CapabilityRequested events being delivered out of order.
    /// See fxb/76579.
    async fn run_test_processor(
        mut stream: ftest_debug::DebugDataProcessorRequestStream,
        mut debug_vmo_recevied_sender: mpsc::Sender<()>,
    ) {
        let req = stream.try_next().await.expect("get first request").unwrap();
        let dir = match req {
            ftest_debug::DebugDataProcessorRequest::SetDirectory { directory, .. } => {
                directory.into_proxy().expect("convert to proxy")
            }
            other => panic!("First request should be SetDirectory but got {:?}", other),
        };

        let mut collected_vmos = vec![];
        let mut finish_responder = None;
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
                    let num_vmos = vmos.len();
                    collected_vmos.append(&mut vmos);
                    let _ = responder.send();
                    for _ in 0..num_vmos {
                        let _ = debug_vmo_recevied_sender.send(()).await;
                    }
                }
                ftest_debug::DebugDataProcessorRequest::Finish { responder, .. } => {
                    finish_responder = Some(responder);
                    break;
                }
            }
        }

        for ftest_debug::DebugVmo { data_sink, test_url, .. } in collected_vmos {
            let file = fuchsia_fs::open_file(
                &dir,
                data_sink.as_ref(),
                OpenFlags::CREATE | OpenFlags::RIGHT_WRITABLE,
            )
            .expect("open file");
            fuchsia_fs::write_file(&file, &test_url).await.expect("write file");
        }
        finish_responder.unwrap().send().unwrap();
    }

    async fn construct_test_realm(
        sender: DebugDataSender,
        test_url: &'static str,
    ) -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;

        let processor = builder
            .add_local_child(
                "processor",
                move |handles| {
                    Box::pin(serve_debug_data_publisher(
                        handles,
                        test_url.to_string(),
                        sender.clone(),
                    ))
                },
                ChildOptions::new().eager(),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<fdebug::PublisherMarker>())
                    .from(&processor)
                    .to(Ref::parent()),
            )
            .await?;

        let instance = builder.build().await?;
        Ok(instance)
    }

    fn isolated_dir() -> DebugDataDirectory {
        DebugDataDirectory::Isolated { parent: "/tmp" }
    }

    #[fuchsia::test]
    async fn serve_no_requests() {
        const TEST_URL: &str = "test-url";
        let DebugDataForTestResult { processor, sender, stream } =
            DebugDataProcessor::new_for_test(isolated_dir());
        let test_realm = construct_test_realm(sender, TEST_URL).await.expect("build test realm");
        test_realm.destroy().await.expect("destroy test realm");

        let (event_sender, event_recv) = mpsc::channel(1);
        processor.collect_and_serve(event_sender).await.unwrap();

        assert!(stream.collect::<Vec<_>>().await.is_empty());
        assert!(event_recv.collect::<Vec<_>>().await.is_empty());
    }

    #[fuchsia::test]
    async fn serve_single_client() {
        const TEST_URL: &str = "test-url";
        let DebugDataForTestResult { processor, sender, stream } =
            DebugDataProcessor::new_for_test(isolated_dir());
        let test_realm = construct_test_realm(sender, TEST_URL).await.expect("build test realm");

        let (vmo_request_received_send, vmo_request_received_recv) = mpsc::channel(2);
        // Future running fuchsia.test.debug.DebugDataProcessor.
        let processor_server_fut = run_test_processor(stream, vmo_request_received_send);
        // Future running the 'test' (client of fuchsia.debugdata.Publisher)
        let test_fut = async move {
            let proxy = test_realm
                .root
                .connect_to_protocol_at_exposed_dir::<fdebug::PublisherMarker>()
                .expect("connect to publisher");
            let vmo_1 = zx::Vmo::create(VMO_SIZE).unwrap();
            let (vmo_token_1, vmo_token_server_1) = zx::EventPair::create().unwrap();
            proxy.publish("data-sink-1", vmo_1, vmo_token_server_1).expect("publish vmo");
            drop(vmo_token_1);
            let vmo_2 = zx::Vmo::create(VMO_SIZE).unwrap();
            let (vmo_token_2, vmo_token_server_2) = zx::EventPair::create().unwrap();
            proxy.publish("data-sink-2", vmo_2, vmo_token_server_2).expect("publish vmo");
            drop(vmo_token_2);
            drop(proxy);

            vmo_request_received_recv.take(1).collect::<()>().await;
            test_realm.destroy().await.expect("destroy test realm");
        };

        let (event_sender, event_recv) = mpsc::channel(10);
        // Future that collects VMOs from the test realm and forwards
        // them to fuchsia.debugdata.Publisher
        let processor_fut = processor
            .collect_and_serve(event_sender)
            .unwrap_or_else(|e| panic!("processor failed: {:?}", e));
        // Future that collects produced debug artifact and asserts on contents.
        let assertion_fut = async move {
            let mut events: Vec<_> = event_recv.collect().await;
            assert_eq!(events.len(), 1);
            let RunEventPayload::DebugData(iterator) = events.pop().unwrap().into_payload();
            let iterator_proxy = iterator.into_proxy().unwrap();
            let files: HashSet<_> = stream_fn(move || iterator_proxy.get_next())
                .and_then(|debug_data| async move {
                    Ok((
                        debug_data.name.unwrap(),
                        fuchsia_fs::read_file(&debug_data.file.unwrap().into_proxy().unwrap())
                            .await
                            .expect("read file"),
                    ))
                })
                .try_collect()
                .await
                .expect("file collection");
            let expected = hashset! {
                ("data-sink-1".to_string(), TEST_URL.to_string()),
                ("data-sink-2".to_string(), TEST_URL.to_string()),
            };
            assert_eq!(files, expected);
        };

        futures::future::join4(processor_server_fut, test_fut, processor_fut, assertion_fut).await;
    }

    #[fuchsia::test]
    async fn serve_multiple_client() {
        const TEST_URL: &str = "test-url";
        let DebugDataForTestResult { processor, sender, stream } =
            DebugDataProcessor::new_for_test(isolated_dir());
        let test_realm = construct_test_realm(sender, TEST_URL).await.expect("build test realm");

        let (vmo_request_received_send, vmo_request_received_recv) = mpsc::channel(2);
        // Future running fuchsia.test.debug.DebugDataProcessor.
        let processor_server_fut = run_test_processor(stream, vmo_request_received_send);
        // Future running the 'test' (client of fuchsia.debugdata.Publisher)
        let test_fut = async move {
            let proxy_1 = test_realm
                .root
                .connect_to_protocol_at_exposed_dir::<fdebug::PublisherMarker>()
                .expect("connect to publisher");
            let vmo_1 = zx::Vmo::create(VMO_SIZE).unwrap();
            let (vmo_token_1, vmo_token_server_1) = zx::EventPair::create().unwrap();
            proxy_1.publish("data-sink-1", vmo_1, vmo_token_server_1).expect("publish vmo");
            drop(vmo_token_1);
            let proxy_2 = test_realm
                .root
                .connect_to_protocol_at_exposed_dir::<fdebug::PublisherMarker>()
                .expect("connect to publisher");
            let vmo_2 = zx::Vmo::create(VMO_SIZE).unwrap();
            let (vmo_token_2, vmo_token_server_2) = zx::EventPair::create().unwrap();
            proxy_2.publish("data-sink-2", vmo_2, vmo_token_server_2).expect("publish vmo");
            drop(vmo_token_2);
            drop(proxy_1);
            drop(proxy_2);

            vmo_request_received_recv.take(2).collect::<()>().await;
            test_realm.destroy().await.expect("destroy test realm");
        };

        let (event_sender, event_recv) = mpsc::channel(10);
        // Future that collects VMOs from the test realm and forwards
        // them to fuchsia.debugdata.Publisher
        let processor_fut = processor
            .collect_and_serve(event_sender)
            .unwrap_or_else(|e| panic!("processor failed: {:?}", e));
        // Future that collects produced debug artifact and asserts on contents.
        let assertion_fut = async move {
            let mut events: Vec<_> = event_recv.collect().await;
            assert_eq!(events.len(), 1);
            let RunEventPayload::DebugData(iterator) = events.pop().unwrap().into_payload();
            let iterator_proxy = iterator.into_proxy().unwrap();
            let files: HashSet<_> = stream_fn(move || iterator_proxy.get_next())
                .and_then(|debug_data| async move {
                    Ok((
                        debug_data.name.unwrap(),
                        fuchsia_fs::read_file(&debug_data.file.unwrap().into_proxy().unwrap())
                            .await
                            .expect("read file"),
                    ))
                })
                .try_collect()
                .await
                .expect("file collection");
            let expected = hashset! {
                ("data-sink-1".to_string(), TEST_URL.to_string()),
                ("data-sink-2".to_string(), TEST_URL.to_string()),
            };
            assert_eq!(files, expected);
        };

        futures::future::join4(processor_server_fut, test_fut, processor_fut, assertion_fut).await;
    }

    #[fuchsia::test]
    fn single_publisher_connection_send_vmo_when_ready() {
        const TEST_URL: &str = "test-url";
        let mut executor = fasync::TestExecutor::new().unwrap();

        let (vmo_send, vmo_recv) = mpsc::channel(5);
        let mut vmo_chunk_stream = vmo_recv.ready_chunks(5).boxed();
        let (publisher_proxy, publisher_stream) =
            create_proxy_and_stream::<fdebug::PublisherMarker>().unwrap();
        let mut serve_fut =
            serve_publisher(publisher_stream, TEST_URL, DebugDataSender { sender: vmo_send })
                .boxed();

        let vmo_1 = zx::Vmo::create(VMO_SIZE).unwrap();
        let (vmo_token_1, vmo_token_server_1) = zx::EventPair::create().unwrap();
        let vmo_2 = zx::Vmo::create(VMO_SIZE).unwrap();
        let (vmo_token_2, vmo_token_server_2) = zx::EventPair::create().unwrap();

        publisher_proxy.publish("data-sink-1", vmo_1, vmo_token_server_1).expect("publish vmo");
        publisher_proxy.publish("data-sink-2", vmo_2, vmo_token_server_2).expect("publish vmo");
        drop(vmo_token_1);

        // After this point vmo 1 should be ready for processing and passed on to processor, but
        // vmo 2 should not.

        assert!(executor.run_until_stalled(&mut serve_fut).is_pending());
        let mut ready_vmos = match executor.run_until_stalled(&mut vmo_chunk_stream.next().boxed())
        {
            Poll::Pending => panic!("vmos should be ready"),
            Poll::Ready(Some(vmos)) => vmos,
            Poll::Ready(None) => panic!("stream closed prematurely"),
        };
        assert_eq!(ready_vmos.len(), 1);
        let ready_vmo = ready_vmos.pop().unwrap();
        assert_eq!(ready_vmo.test_url.as_str(), TEST_URL);
        assert_eq!(ready_vmo.data_sink.as_str(), "data-sink-1");

        // After dropping vmo token 2 it should be passed to the processor.
        drop(vmo_token_2);
        drop(publisher_proxy);
        match executor.run_until_stalled(&mut serve_fut) {
            futures::task::Poll::Ready(Ok(())) => (),
            other => panic!("Expected poll to be ready but was {:?}", other),
        }

        let mut ready_vmos = match executor.run_until_stalled(&mut vmo_chunk_stream.next().boxed())
        {
            Poll::Pending => panic!("vmos should be ready"),
            Poll::Ready(Some(vmos)) => vmos,
            Poll::Ready(None) => panic!("stream closed prematurely"),
        };
        assert_eq!(ready_vmos.len(), 1);
        let ready_vmo = ready_vmos.pop().unwrap();
        assert_eq!(ready_vmo.test_url.as_str(), TEST_URL);
        assert_eq!(ready_vmo.data_sink.as_str(), "data-sink-2");

        match executor.run_until_stalled(&mut vmo_chunk_stream.next().boxed()) {
            Poll::Pending => panic!("vmos should be ready"),
            Poll::Ready(None) => (),
            Poll::Ready(Some(vmos)) => panic!("Expected stream to terminate but got {:?}", vmos),
        };
    }
}
