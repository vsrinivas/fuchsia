// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error, fidl::endpoints::RequestStream,
    fidl_test_componentmanager_stresstests as fstresstests, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs, fuchsia_syslog::fx_log_debug, futures::prelude::*,
};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["child_for_stress_test"])?;
    fx_log_debug!("started");
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |mut stream: fstresstests::LifecycleRequestStream| {
        fasync::Task::local(async move {
            stream.control_handle().send_on_connected().unwrap();
            while let Some(event) = stream.try_next().await.expect("Cannot read request stream") {
                match event {
                    fstresstests::LifecycleRequest::Stop { .. } => {
                        std::process::exit(0);
                    }
                }
            }
        })
    });
    fs.take_and_serve_directory_handle()?;
    fs.for_each_concurrent(None, |t| async {
        t.await;
    })
    .await;
    Ok(())
}
