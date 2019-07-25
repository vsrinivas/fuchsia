// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use failure::{Error, ResultExt};
use fidl_fuchsia_update::{
    ChannelControlRequest, ChannelControlRequestStream, ChannelWriterRequest,
    ChannelWriterRequestStream, Slot,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

enum IncomingServices {
    ChannelWriter(ChannelWriterRequestStream),
    ChannelControl(ChannelControlRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(IncomingServices::ChannelWriter)
        .add_fidl_service(IncomingServices::ChannelControl);
    fs.take_and_serve_directory_handle()?;

    let mut current_slot = Slot::A;
    let mut current_data_blob: Option<Vec<u8>> = None;
    let mut target_channel = String::new();
    while let Some(service) = await!(fs.next()) {
        match service {
            IncomingServices::ChannelWriter(mut stream) => {
                while let Some(request) =
                    await!(stream.try_next()).context("error receiving ChannelWriter request")?
                {
                    match request {
                        ChannelWriterRequest::GetChannelData { responder } => {
                            let mut data_blob =
                                current_data_blob.as_ref().map(|v| v.iter().cloned());
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
            IncomingServices::ChannelControl(mut stream) => {
                while let Some(request) =
                    await!(stream.try_next()).context("error receiving ChannelControl request")?
                {
                    match request {
                        ChannelControlRequest::GetChannel { responder } => {
                            responder.send("fake-current-channel")?;
                        }
                        ChannelControlRequest::GetTarget { responder } => {
                            responder.send(&target_channel)?;
                        }
                        ChannelControlRequest::SetTarget { channel, responder } => {
                            target_channel = channel;
                            responder.send()?;
                        }
                    }
                }
            }
        }
    }

    Ok(())
}
