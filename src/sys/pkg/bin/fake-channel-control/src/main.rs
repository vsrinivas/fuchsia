// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use anyhow::{Context as _, Error};
use fidl_fuchsia_update_channelcontrol::{ChannelControlRequest, ChannelControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use futures::prelude::*;

enum IncomingServices {
    ChannelControl(ChannelControlRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(IncomingServices::ChannelControl);
    fs.take_and_serve_directory_handle().context("while serving directory handle")?;

    let mut target_channel = String::new();
    while let Some(service) = fs.next().await {
        match service {
            IncomingServices::ChannelControl(mut stream) => {
                while let Some(request) =
                    stream.try_next().await.context("error receiving ChannelControl request")?
                {
                    match request {
                        ChannelControlRequest::GetCurrent { responder } => {
                            responder.send("fake-current-channel")?;
                        }
                        ChannelControlRequest::GetTarget { responder } => {
                            responder.send(&target_channel)?;
                        }
                        ChannelControlRequest::SetTarget { channel, responder } => {
                            target_channel = channel;
                            responder.send()?;
                        }
                        ChannelControlRequest::GetTargetList { responder } => {
                            responder.send(
                                &mut vec!["fake-current-channel", "other-channel"].into_iter(),
                            )?;
                        }
                    }
                }
            }
        }
    }

    Ok(())
}
