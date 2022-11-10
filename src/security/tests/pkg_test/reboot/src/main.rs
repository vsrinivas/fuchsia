// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async::Task, fuchsia_component::server::ServiceFs, futures::StreamExt,
    mock_reboot::MockRebootService, std::sync::Arc, tracing::info,
};

#[fuchsia::main]
async fn main() {
    info!("Starting mock reboot component");
    let reboot_service = Arc::new(MockRebootService::new(Box::new(|reboot_reason| {
        info!(?reboot_reason, "fuchsia.hardware.power.statecontrol.Admin",);
        Ok(())
    })));
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        info!("Starting mock reboot service");
        let reboot_service = Arc::clone(&reboot_service);
        Task::spawn(async move {
            info!("Running mock reboot service");
            reboot_service.run_reboot_service(stream).await.unwrap();
        })
        .detach()
    });
    fs.take_and_serve_directory_handle().unwrap();
    fs.collect::<()>().await;
}
