// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_debug_implementations)]
#![warn(rust_2018_idioms)]
#![warn(clippy::all)]

mod async_condition;
mod dummy_device;
mod lowpan_device;
mod serve_to;

pub use async_condition::*;
pub use dummy_device::DummyDevice;
pub use lowpan_device::Driver;
pub use serve_to::*;

use anyhow::{format_err, Context as _};
use async_trait::async_trait;
use fidl_fuchsia_lowpan_device::DriverRequest;
use fuchsia_syslog::macros::*;
use fuchsia_zircon_status::Status as ZxStatus;
use futures::future::{join_all, ready};
use futures::prelude::*;

/// A `Result` that uses `fuchsia_zircon::Status` for the error condition.
pub type ZxResult<T> = Result<T, ZxStatus>;

const MAX_CONCURRENT: usize = 100;

/// Registers a driver instance with the given LoWPAN service and returns
/// a future which services requests for the driver.
pub async fn register_and_serve_driver<'a, R, D>(
    name: &str,
    registry: R,
    driver: &'a D,
) -> anyhow::Result<()>
where
    R: fidl_fuchsia_lowpan_device::RegisterProxyInterface,
    D: Driver + 'a,
{
    use fidl::endpoints::create_endpoints;
    use fidl_fuchsia_lowpan_device::DriverMarker;

    let (client_ep, server_ep) =
        create_endpoints::<DriverMarker>().context("Failed to create FIDL endpoints")?;

    registry
        .register_device(name, client_ep)
        .map(|x| match x {
            Ok(Ok(x)) => Ok(x),
            Ok(Err(err)) => Err(format_err!("Service Error: {:?}", err)),
            Err(err) => Err(err.into()),
        })
        .await
        .context("Failed to register LoWPAN device driver")?;

    server_ep
        .into_stream()?
        .try_for_each_concurrent(MAX_CONCURRENT, |cmd| async {
            match cmd {
                DriverRequest::GetProtocols { protocols, .. } => {
                    let mut futures = vec![];
                    if let Some(server_end) = protocols.device {
                        if let Some(stream) = server_end.into_stream().ok() {
                            futures.push(driver.serve_to(stream));
                        }
                    }
                    if let Some(server_end) = protocols.device_extra {
                        if let Some(stream) = server_end.into_stream().ok() {
                            futures.push(driver.serve_to(stream));
                        }
                    }
                    if let Some(server_end) = protocols.device_test {
                        if let Some(stream) = server_end.into_stream().ok() {
                            futures.push(driver.serve_to(stream));
                        }
                    }
                    let _ = join_all(futures).await;
                }
            }
            Ok(())
        })
        .await?;

    fx_log_info!("LoWPAN Driver {:?} Stopped.", name);

    Ok(())
}
