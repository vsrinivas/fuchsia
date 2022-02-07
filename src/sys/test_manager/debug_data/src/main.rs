// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod data_processor;
mod debug_data_server;
mod debug_data_set;
mod iterator;
mod message;

use crate::message::DebugDataRequestMessage;
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_sys2 as fsys;
use fidl_fuchsia_test_debug as ftest_debug;
use fidl_fuchsia_test_internal as ftest_internal;
use fidl_fuchsia_test_manager as ftest_manager;
use fuchsia_component::{client::connect_to_protocol, server::ServiceFs};
use futures::{channel::mpsc, pin_mut, FutureExt, StreamExt};
use log::info;

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    info!("started");

    let event_source = connect_to_protocol::<fsys::EventSourceMarker>()?;
    let event_stream = event_source.take_static_event_stream("EventStream").await?.unwrap();

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

    futures::future::join(
        debug_data_set::handle_debug_data_controller_and_events(
            request_stream_recv.flatten(),
            event_stream.into_stream()?,
            DebugRequestHandlerImpl,
            fuchsia_inspect::component::inspector().root(),
        ),
        fs.collect::<()>(),
    )
    .await;
    Ok(())
}

struct DebugRequestHandlerImpl;

#[async_trait[?Send]]
impl debug_data_set::DebugRequestHandler for DebugRequestHandlerImpl {
    async fn handle_debug_requests(
        &self,
        debug_request_recv: mpsc::Receiver<DebugDataRequestMessage>,
        iter: ftest_manager::DebugDataIteratorRequestStream,
    ) -> Result<(), Error> {
        let (vmo_send, vmo_recv) = mpsc::channel(5);
        let debug_data_handler_fut =
            debug_data_server::serve_debug_data_requests(debug_request_recv, vmo_send).map(Ok);
        let process_and_iterator_fut = async move {
            let peekable = vmo_recv.peekable();
            pin_mut!(peekable);
            // Don't create a connection to the processor in the common case where no debug data is
            // produced.
            if peekable.as_mut().peek().await.is_some() {
                let processor = connect_to_protocol::<ftest_debug::DebugDataProcessorMarker>()?;
                // We give all the processors the same directory for now, as infra scripts rely on
                // the location of the produced profiles. We should give these different
                // directories once these are changed.
                data_processor::process_debug_data_vmos("/data", processor, peekable).await?;
                iterator::serve_iterator("/data", iter).await
            } else {
                Ok(())
            }
        };
        futures::future::try_join(debug_data_handler_fut, process_and_iterator_fut).await?;
        Ok(())
    }
}
