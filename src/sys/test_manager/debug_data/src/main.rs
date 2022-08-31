// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod data_processor;
mod debug_data_server;
mod debug_data_set;
mod iterator;
mod message;

use crate::message::PublisherRequestMessage;
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_sys2 as fsys;
use fidl_fuchsia_test_debug as ftest_debug;
use fidl_fuchsia_test_internal as ftest_internal;
use fidl_fuchsia_test_manager as ftest_manager;
use fuchsia_component::client::connect_to_protocol_at_path;
use fuchsia_component::{client::connect_to_protocol, server::ServiceFs};
use fuchsia_zircon as zx;
use futures::{channel::mpsc, pin_mut, FutureExt, StreamExt};
use tracing::info;

/// Timeout after Finish() is sent for a set to process events.
const TIMEOUT_AFTER_FINISH: zx::Duration = zx::Duration::from_seconds(20);

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    info!("started");

    let mut fs = ServiceFs::new();
    inspect_runtime::serve(fuchsia_inspect::component::inspector(), &mut fs)?;

    // Here we send debug data requests to another future, so that we can poll
    // the fs stream separately from the future that handles debug data requests.
    // This seems to be necessary to ensure that futures serving inspect are polled.
    let (request_stream_send, request_stream_recv) = mpsc::unbounded();
    fs.dir("svc").add_fidl_service(
        move |request: ftest_internal::DebugDataControllerRequestStream| {
            let _ = request_stream_send.unbounded_send(request);
        },
    );
    fs.take_and_serve_directory_handle()?;
    let event_stream =
        connect_to_protocol_at_path::<fsys::EventStream2Marker>("/events/event_stream").unwrap();

    futures::future::join(
        debug_data_set::handle_debug_data_controller_and_events(
            request_stream_recv.flatten(),
            event_stream,
            PublisherHandlerImpl,
            TIMEOUT_AFTER_FINISH,
            fuchsia_inspect::component::inspector().root(),
        ),
        fs.collect::<()>(),
    )
    .await;
    Ok(())
}

struct PublisherHandlerImpl;

#[async_trait[?Send]]
impl debug_data_set::PublishRequestHandler for PublisherHandlerImpl {
    async fn handle_publish_requests(
        &self,
        publish_request_recv: mpsc::Receiver<PublisherRequestMessage>,
        iter: ftest_manager::DebugDataIteratorRequestStream,
        accumulate: bool,
    ) -> Result<(), Error> {
        // When accumulate is true, give all the processors the same directory.
        // Infra scripts currently rely on the location of the produced profiles.
        let tmp_dir = match accumulate {
            true => None,
            false => Some(tempfile::tempdir_in("/cache")?),
        };
        let dir_path = match tmp_dir.as_ref() {
            Some(dir) => dir.path().to_str().unwrap(),
            None => "/data",
        };

        let (vmo_send, vmo_recv) = mpsc::channel(5);
        let publisher_handler_fut =
            debug_data_server::serve_publisher_requests(publish_request_recv, vmo_send).map(Ok);
        let process_and_iterator_fut = async move {
            let peekable = vmo_recv.peekable();
            pin_mut!(peekable);
            // Don't create a connection to the processor in the common case where no debug data is
            // produced.
            if peekable.as_mut().peek().await.is_some() {
                let processor = connect_to_protocol::<ftest_debug::DebugDataProcessorMarker>()?;
                data_processor::process_debug_data_vmos(dir_path, processor, peekable).await?;
                iterator::serve_iterator(dir_path, iter).await
            } else {
                Ok(())
            }
        };
        futures::future::try_join(publisher_handler_fut, process_and_iterator_fut).await?;
        Ok(())
    }
}
