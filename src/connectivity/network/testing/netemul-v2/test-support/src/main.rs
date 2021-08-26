// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_netemul_test::{CounterRequest, CounterRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::{
        client,
        server::{ServiceFs, ServiceFsDir},
    },
    futures::prelude::*,
    log::{error, info},
    std::sync::Arc,
    std::sync::Mutex,
};

struct CounterData {
    value: u32,
}

const SVC_DIR: &str = "/svc";

async fn handle_counter(
    stream: CounterRequestStream,
    data: Arc<Mutex<CounterData>>,
) -> Result<(), fidl::Error> {
    stream
        .try_for_each(|request| async {
            match request {
                CounterRequest::Increment { responder } => {
                    let mut d = data.lock().unwrap();
                    d.value += 1;
                    info!("incrementing counter to {}", d.value);
                    let () = responder
                        .send(d.value)
                        .unwrap_or_else(|e| error!("error sending response: {:?}", e));
                }
                CounterRequest::ConnectToProtocol { protocol_name, request, control_handle: _ } => {
                    info!("connecting to protocol '{}'", protocol_name);
                    let () = client::connect_channel_to_protocol_at_path(
                        request,
                        &format!("{}/{}", SVC_DIR, protocol_name),
                    )
                    .unwrap_or_else(|e| {
                        error!(
                            "error connecting request to protocol '{}' in '{}' directory: {:?}",
                            protocol_name, SVC_DIR, e,
                        )
                    });
                }
                CounterRequest::OpenInNamespace { path, flags, request, control_handle: _ } => {
                    info!("connecting to node at '{}'", path);
                    let () = fdio::open(&path, flags, request).unwrap_or_else(|e| {
                        error!("error connecting request to node at path '{}': {}", path, e)
                    });
                }
                CounterRequest::TryOpenDirectory { path, responder } => {
                    info!("opening directory at '{}'", path);
                    match std::fs::read_dir(&path) {
                        Ok(std::fs::ReadDir { .. }) => responder
                            .send(&mut Ok(()))
                            .unwrap_or_else(|e| error!("error sending response: {:?}", e)),
                        Err(e) => {
                            let status = match e.kind() {
                                std::io::ErrorKind::NotFound | std::io::ErrorKind::BrokenPipe => {
                                    info!("failed to open directory at '{}': {}", path, e);
                                    fuchsia_zircon::Status::NOT_FOUND
                                }
                                _ => {
                                    error!("failed to open directory at '{}': {}", path, e);
                                    fuchsia_zircon::Status::IO
                                }
                            };
                            let () = responder
                                .send(&mut Err(status.into_raw()))
                                .unwrap_or_else(|e| error!("error sending response: {:?}", e));
                        }
                    }
                }
            }
            Ok(())
        })
        .await
}

/// Command line arguments for the counter service.
#[derive(argh::FromArgs)]
struct Args {
    /// the value at which to start the counter.
    #[argh(option, default = "0")]
    starting_value: u32,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let Args { starting_value } = argh::from_env();
    let () = fuchsia_syslog::init().context("cannot init logger")?;
    let mut fs = ServiceFs::new();
    let inspector = fuchsia_inspect::component::inspector();
    let data = {
        let data = Arc::new(Mutex::new(CounterData { value: starting_value }));
        let data_clone = data.clone();
        let () = inspector.root().record_lazy_child("counter", move || {
            let srv = fuchsia_inspect::Inspector::new();
            let () = srv.root().record_uint(
                "count",
                data.lock().expect("failed to acquire lock on `CounterData`").value.into(),
            );
            futures::future::ok(srv).boxed()
        });
        data_clone
    };
    let () = inspect_runtime::serve(inspector, &mut fs).context("error serving inspect")?;

    let _: &mut ServiceFsDir<'_, _> = fs.dir("svc").add_fidl_service(|s: CounterRequestStream| s);
    let _: &mut ServiceFs<_> =
        fs.take_and_serve_directory_handle().context("error serving directory handle")?;
    let () = fs
        .for_each_concurrent(None, |stream| async {
            handle_counter(stream, data.clone())
                .await
                .unwrap_or_else(|e| error!("error handling CounterRequestStream: {:?}", e))
        })
        .await;
    Ok(())
}
