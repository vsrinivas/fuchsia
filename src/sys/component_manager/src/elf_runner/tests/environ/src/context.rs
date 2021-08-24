// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_elf_test as fet, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
    std::env,
};

#[fuchsia::component]
async fn main() {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            run_context_service(stream).await;
        })
        .detach();
    });
    fs.take_and_serve_directory_handle().expect("failed to serve outgoing directory");
    fs.collect::<()>().await;
}

async fn run_context_service(stream: fet::ContextRequestStream) {
    let () = stream
        .try_for_each_concurrent(None, |request| async {
            match request {
                fet::ContextRequest::GetEnviron { responder } => {
                    let mut environ: Vec<String> = vec![];
                    for (key, value) in env::vars() {
                        environ.push(format!("{}={}", key, value));
                    }
                    responder
                        .send(&mut environ.iter().map(|s| &s[..]))
                        .expect("failed to send environ");
                }
            }
            Ok(())
        })
        .await
        .expect("fail to serve stream");
}
