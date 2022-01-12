// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod debug_data_set;
mod message;

use crate::message::DebugDataRequestMessage;
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_sys2 as fsys;
use fidl_fuchsia_test_internal as ftest_internal;
use fidl_fuchsia_test_manager as ftest_manager;
use fuchsia_component::{client::connect_to_protocol, server::ServiceFs};
use futures::{channel::mpsc, StreamExt};
use log::info;

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    info!("started");

    let event_source = connect_to_protocol::<fsys::EventSourceMarker>()?;
    let event_stream = event_source.take_static_event_stream("EventStream").await?.unwrap();

    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(|request: ftest_internal::DebugDataControllerRequestStream| request);
    fs.take_and_serve_directory_handle()?;
    let debug_data_controller_stream = fs.flatten();

    debug_data_set::handle_debug_data_controller_and_events(
        debug_data_controller_stream,
        event_stream.into_stream()?,
        DebugRequestHandlerImpl,
    )
    .await;
    Ok(())
}

struct DebugRequestHandlerImpl;

#[async_trait[?Send]]
impl debug_data_set::DebugRequestHandler for DebugRequestHandlerImpl {
    async fn handle_debug_requests(
        &self,
        _debug_request_recv: mpsc::Receiver<DebugDataRequestMessage>,
        _iter: ftest_manager::DebugDataIteratorRequestStream,
    ) -> Result<(), Error> {
        // TODO(satsukiu): actually process the data
        Ok(())
    }
}
