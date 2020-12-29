// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_test_log_stdio::{StdioPuppetRequest, StdioPuppetRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog as syslog;
use futures::prelude::*;
use log::{error, info};

/// Serves `StdioPuppet` requests received through the provided stream.
async fn run_stdio_puppet(mut stream: StdioPuppetRequestStream) -> Result<(), Error> {
    while let Some(request) = stream.try_next().await? {
        match request {
            StdioPuppetRequest::WritelnStdout { line, .. } => {
                println!("{}", line);
            }
            StdioPuppetRequest::WritelnStderr { line, .. } => {
                eprintln!("{}", line);
            }
        }
    }
    Ok(())
}

enum PuppetServices {
    StdioPuppet(StdioPuppetRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&[]).expect("should not fail");
    info!("Puppet starting");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(PuppetServices::StdioPuppet);

    fs.take_and_serve_directory_handle()?;

    // Although today, only one client is expected, use for_each_concurrent so that
    // multiple clients may be served by increasing MAX_CONCURRENT.
    const MAX_CONCURRENT: usize = 1;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |PuppetServices::StdioPuppet(stream)| {
        run_stdio_puppet(stream).unwrap_or_else(|e| error!("ERROR in puppet's main: {:?}", e))
    });

    fut.await;
    Ok(())
}
