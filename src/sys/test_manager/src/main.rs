// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_info, fx_log_warn},
    futures::StreamExt,
};

fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["test_manager"])?;
    fx_log_info!("started");
    let mut executor = fasync::Executor::new().context("error creating executor")?;
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn_local(async move {
            test_manager_lib::run_test_manager(stream)
                .await
                .unwrap_or_else(|e| fx_log_warn!("test manager returned error: {:?}", e))
        });
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}
