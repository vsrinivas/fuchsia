// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async::{futures::StreamExt, Task},
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_info, init},
    mock_reboot::MockRebootService,
    std::sync::Arc,
};

#[fuchsia_async::run_singlethreaded]
async fn main() {
    init().unwrap();
    fx_log_info!("Starting mock reboot component");
    let reboot_service = Arc::new(MockRebootService::new(Box::new(|reboot_reason| {
        fx_log_info!(
            "fuchsia.hardware.power.statecontrol.Admin reboot reason: {:#?}",
            reboot_reason
        );
        Ok(())
    })));
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        fx_log_info!("Starting mock reboot service");
        let reboot_service = Arc::clone(&reboot_service);
        Task::spawn(async move {
            fx_log_info!("Running mock reboot service");
            reboot_service.run_reboot_service(stream).await.unwrap();
        })
        .detach()
    });
    fs.take_and_serve_directory_handle().unwrap();
    fs.collect::<()>().await;
}
