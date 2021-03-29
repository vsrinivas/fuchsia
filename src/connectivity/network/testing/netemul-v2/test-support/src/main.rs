// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_netemul_test::{CounterRequest, CounterRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceFsDir},
    futures::prelude::*,
    log::{error, info},
    std::sync::Arc,
    std::sync::Mutex,
};

struct CounterData {
    value: u32,
}

async fn handle_counter(
    stream: CounterRequestStream,
    data: Arc<Mutex<CounterData>>,
) -> Result<(), fidl::Error> {
    stream
        .try_for_each(|CounterRequest::Increment { responder }| async {
            let mut d = data.lock().unwrap();
            d.value += 1;
            info!("incrementing counter to {}", d.value);
            let () = responder
                .send(d.value)
                .unwrap_or_else(|e| error!("error sending response: {:?}", e));
            Ok(())
        })
        .await
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let () = fuchsia_syslog::init().context("cannot init logger")?;
    let mut fs = ServiceFs::new();
    let inspector = fuchsia_inspect::component::inspector();
    let data = {
        let data = Arc::new(Mutex::new(CounterData { value: 0 }));
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
    let () = inspector.serve(&mut fs).context("error serving inspect")?;

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
