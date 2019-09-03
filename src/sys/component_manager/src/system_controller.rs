// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl_fuchsia_sys2::{SystemControllerRequest, SystemControllerRequestStream},
    futures::prelude::*,
    std::process,
};

pub async fn serve(mut stream: SystemControllerRequestStream) -> Result<(), Error> {
    while let Some(req) = stream.try_next().await? {
        match req {
            SystemControllerRequest::Shutdown { responder } => {
                match responder.send() {
                    Ok(()) => {}
                    Err(e) => {
                        println!(
                            "error sending response to shutdown requester:\
                             {}\n shut down proceeding",
                            e
                        );
                    }
                }
                // TODO(jmatt) replace with a call into the model or something to actually stop
                // everthing
                process::exit(0);
            }
        }
    }
    Ok(())
}
