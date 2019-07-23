// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use failure::{Error, ResultExt};
use fidl_fuchsia_update::{ChannelWriterRequest, ChannelWriterRequestStream, Slot};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

enum IncomingServices {
    ChannelWriter(ChannelWriterRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingServices::ChannelWriter);
    fs.take_and_serve_directory_handle()?;

    let mut current_slot = Slot::A;
    let mut current_data_blob: Option<Vec<u8>> = None;
    while let Some(IncomingServices::ChannelWriter(mut stream)) = await!(fs.next()) {
        while let Some(request) =
            await!(stream.try_next()).context("error receiving ChannelWriter request")?
        {
            match request {
                ChannelWriterRequest::GetChannelData { responder } => {
                    let mut data_blob = current_data_blob.as_ref().map(|v| v.iter().cloned());
                    responder.send(
                        current_slot,
                        data_blob
                            .as_mut()
                            .map(|i| i as &mut dyn std::iter::ExactSizeIterator<Item = u8>),
                    )?;
                }
                ChannelWriterRequest::SetChannelData { slot, data_blob, responder } => {
                    // In this fake implementation we also change the current slot for testing
                    // purpose, but in production SetChannelData will not change current slot.
                    current_slot = slot;
                    current_data_blob = Some(data_blob);
                    responder.send(&mut Ok(()))?;
                }
            }
        }
    }

    Ok(())
}
