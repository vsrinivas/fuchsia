// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_test_proxy_stress as fstress,
    fuchsia_component::server::ServiceFs,
    futures::{
        future::TryFutureExt,
        stream::{StreamExt, TryStreamExt},
    },
    tracing::{info, warn},
};

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    info!("started");
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: fstress::StressorRequestStream| stream);
    fs.take_and_serve_directory_handle()?;
    fs.for_each_concurrent(None, |stream| {
        serve_stressor(stream).unwrap_or_else(|e| warn!("Error serving stream: {:?}", e))
    })
    .await;
    Ok(())
}

const BYTES: [u8; 2048] = [0xFFu8; 2048];

async fn serve_stressor(stream: fstress::StressorRequestStream) -> Result<(), Error> {
    stream
        .map_err(Error::from)
        .try_for_each_concurrent(None, |request| async move {
            match request {
                fstress::StressorRequest::StuffSocket { socket, responder } => {
                    let mut bytes_written = 0;
                    loop {
                        match socket.write(&BYTES) {
                            Ok(bytes) => {
                                bytes_written += bytes;
                            }
                            Err(fuchsia_zircon::Status::SHOULD_WAIT) => break,
                            Err(status) => {
                                warn!("Error while writing to socket: {:?}", status);
                                return Err(status.into());
                            }
                        }
                    }
                    info!("Wrote {:?} bytes to socket before full", bytes_written);
                    let _ = responder.send(bytes_written as u32);
                    Ok(())
                }
                fstress::StressorRequest::Echo { content, responder } => {
                    let _ = responder.send(&content);
                    Ok(())
                }
            }
        })
        .await
}
