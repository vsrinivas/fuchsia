// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fuchsia_component::server::ServiceObjLocal;
use futures::lock::Mutex;
use futures::prelude::*;
use std::pin::Pin;
use std::sync::Arc;
// Include Brightness Control FIDL bindings
use backlight::Backlight;
use control::{Control, ControlTrait};
use fidl_fuchsia_ui_brightness::ControlRequestStream;
use fuchsia_async::{self as fasync};
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog::{self, fx_log_info};
use sensor::Sensor;

mod backlight;
mod control;
mod sensor;

async fn run_brightness_server(
    mut stream: ControlRequestStream,
    control: Arc<Mutex<dyn ControlTrait>>,
) -> Result<(), Error> {
    fx_log_info!("New brightness server");
    while let Some(request) = stream.try_next().await.context("error running brightness server")? {
        // TODO(kpt): Make each match a testable function when hanging gets are implemented
        let mut control = control.lock().await;
        control.handle_request(request).await;
    }
    Ok(())
}

async fn run_brightness_service(
    fs: ServiceFs<ServiceObjLocal<'static, ControlRequestStream>>,
    control: Arc<Mutex<dyn ControlTrait>>,
    run_server: impl Fn(
        ControlRequestStream,
        Arc<Mutex<dyn ControlTrait>>,
    ) -> Pin<Box<dyn Future<Output = Result<(), Error>>>>,
) -> Result<(), Error> {
    const MAX_CONCURRENT: usize = 10_000;
    let fut = fs.for_each_concurrent(MAX_CONCURRENT, |stream| {
        let control = control.clone();
        run_server(stream, control).unwrap_or_else(|e| fx_log_info!("{:?}", e))
    });
    fut.await;
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["brightness"])?;
    fx_log_info!("Started");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(|stream: ControlRequestStream| stream);
    fs.take_and_serve_directory_handle()?;

    let backlight = Backlight::new().await?;
    let backlight = Arc::new(Mutex::new(backlight));

    let sensor = Sensor::new().await;
    let sensor = Arc::new(Mutex::new(sensor));

    let control = Control::new(sensor, backlight);
    let control = Arc::new(Mutex::new(control));

    let run_server: fn(
        stream: ControlRequestStream,
        control: Arc<Mutex<dyn ControlTrait>>,
    ) -> Pin<Box<dyn Future<Output = Result<(), Error>>>> =
        |stream, control| Box::pin(run_brightness_server(stream, control));

    // TODO(kpt) Writes FIDL client unit test for this.
    run_brightness_service(fs, control, run_server).await?;
    Ok(())
}
