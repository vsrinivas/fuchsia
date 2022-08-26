// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::prelude::*;
use anyhow::Error;
use std::fs::File;
use std::io::Write;

// Import with more meaningful names
use fidl_fuchsia_lowpan_bootstrap::{
    ThreadRequest as BootstrapThreadRequest, ThreadRequestStream as BootstrapThreadRequestStream,
};

enum IncomingService {
    BootstrapThread(BootstrapThreadRequestStream),
}

async fn process_bootstrap_thread_request_stream(
    stream: BootstrapThreadRequestStream,
) -> Result<(), Error> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async move {
            match request {
                BootstrapThreadRequest::ImportSettings { thread_settings_json, responder } => {
                    let mut bytes = vec![0; thread_settings_json.size as usize];
                    thread_settings_json
                        .vmo
                        .read(&mut bytes, 0)
                        .context("Could not read settings")?;

                    // Write the settings file used by thread.
                    File::create("/data/thread-settings.json")
                        .context("Could not create thread-settings.json file")?
                        .write_all(&bytes)
                        .context("Writing to settings file failed")?;

                    // Write the initial settings file which remains unchanged
                    // and is used for verification.
                    File::create("/data/thread-settings-init.json")
                        .context("Could not create thread-settings-init.json file")?
                        .write_all(&bytes)
                        .context("Writing to init-settings file failed")?;

                    responder.send().context("Failed to send a response")?;
                }
            }
            Ok(())
        })
        .await
}

pub async fn bootstrap_thread() -> Result<(), Error> {
    let mut fs = fuchsia_component::server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(IncomingService::BootstrapThread);
    fs.take_and_serve_directory_handle()?;

    const MAX_CONCURRENT: usize = 10;

    fs.for_each_concurrent(MAX_CONCURRENT, |IncomingService::BootstrapThread(stream)| {
        process_bootstrap_thread_request_stream(stream).unwrap_or_else(|e| warn!("{:?}", e))
    })
    .await;

    Ok(())
}
