// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::prelude::*,
    fidl::AsyncChannel,
    fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::{component, health::Reporter},
    fuchsia_runtime::{take_startup_handle, HandleInfo, HandleType},
    fuchsia_zircon as zx,
    futures::{StreamExt, TryStreamExt},
    tracing::info,
};

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    info!("Started diagnostics publisher");
    let mut fs = ServiceFs::new();
    inspect_runtime::serve(component::inspector(), &mut fs)?;
    component::health().set_ok();
    fs.take_and_serve_directory_handle()?;
    fasync::Task::spawn(fs.collect::<()>()).detach();

    match take_startup_handle(HandleInfo::new(HandleType::Lifecycle, 0)) {
        Some(lifecycle_handle) => {
            let chan: zx::Channel = lifecycle_handle.into();
            let async_chan = AsyncChannel::from(
                fasync::Channel::from_channel(chan).expect("Async channel conversion failed."),
            );
            let mut req_stream = LifecycleRequestStream::from_channel(async_chan);
            if let Some(LifecycleRequest::Stop { control_handle: c }) =
                req_stream.try_next().await.expect("Failure receiving lifecycle FIDL message")
            {
                info!("Finishing through Stop");
                c.shutdown();
                std::process::exit(0);
            }
            unreachable!("Unexpected closure of the lifecycle channel");
        }
        None => {
            info!("Finishing normally");
            std::process::abort();
        }
    }
}
