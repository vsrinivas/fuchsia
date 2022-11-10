// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async::Task, fuchsia_component::server::ServiceFs, futures::StreamExt,
    mock_paver::MockPaverServiceBuilder, std::sync::Arc, tracing::info,
};

#[fuchsia::main]
async fn main() {
    info!("Starting mock paver component");
    let paver_service = Arc::new(MockPaverServiceBuilder::new().build());
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        info!("Starting mock paver service");
        let paver_service = Arc::clone(&paver_service);
        Task::spawn(async move {
            info!("Running mock paver service");
            paver_service.run_paver_service(stream).await.unwrap();
        })
        .detach()
    });
    fs.take_and_serve_directory_handle().unwrap();
    fs.collect::<()>().await;
}
