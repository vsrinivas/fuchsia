// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

use crate::{Driver, ServeTo, MAX_CONCURRENT};
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_lowpan_device::{DriverMarker, DriverRequest, DriverRequestStream};
use futures::future::join_all;

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

    driver.serve_to(server_ep.into_stream()?).await?;

    fx_log_info!("LoWPAN Driver {:?} Stopped.", name);

    Ok(())
}

#[async_trait()]
impl<T: Driver> ServeTo<DriverRequestStream> for T {
    async fn serve_to(&self, request_stream: DriverRequestStream) -> anyhow::Result<()> {
        use std::sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        };
        let legacy_joining_protocol_in_use_flag: Arc<AtomicBool> = Arc::default();

        request_stream
            .try_for_each_concurrent(MAX_CONCURRENT, move |cmd| {
                match cmd {
                    DriverRequest::GetProtocols { protocols, .. } => {
                        let mut futures = vec![];
                        if let Some(server_end) = protocols.device {
                            if let Some(stream) = server_end.into_stream().ok() {
                                futures.push(self.serve_to(stream));
                            }
                        }
                        if let Some(server_end) = protocols.device_extra {
                            if let Some(stream) = server_end.into_stream().ok() {
                                futures.push(self.serve_to(stream));
                            }
                        }
                        if let Some(server_end) = protocols.counters {
                            if let Some(stream) = server_end.into_stream().ok() {
                                futures.push(self.serve_to(stream));
                            }
                        }
                        if let Some(server_end) = protocols.device_test {
                            if let Some(stream) = server_end.into_stream().ok() {
                                futures.push(self.serve_to(stream));
                            }
                        }
                        if let Some(server_end) = protocols.device_route_extra {
                            if let Some(stream) = server_end.into_stream().ok() {
                                futures.push(self.serve_to(stream));
                            }
                        }
                        if let Some(server_end) = protocols.device_route {
                            if let Some(stream) = server_end.into_stream().ok() {
                                futures.push(self.serve_to(stream));
                            }
                        }
                        if let Some(server_end) = protocols.thread_dataset {
                            if let Some(stream) = server_end.into_stream().ok() {
                                futures.push(self.serve_to(stream));
                            }
                        }
                        if let Some(server_end) = protocols.thread_legacy_joining {
                            if let Some(stream) = server_end.into_stream().ok() {
                                // We only let there be one outstanding instance of this protocol.
                                if !legacy_joining_protocol_in_use_flag
                                    .swap(true, Ordering::Relaxed)
                                {
                                    fx_log_info!("Mutually exclusive thread_legacy_joining channel requested and vended.");
                                    let flag = legacy_joining_protocol_in_use_flag.clone();
                                    futures.push(
                                        self.serve_to(stream)
                                            .inspect(move |_| {
                                                fx_log_info!("thread_legacy_joining channel released.");
                                                flag.store(false, Ordering::Relaxed)
                                            })
                                            .boxed(),
                                    );
                                } else {
                                    fx_log_warn!("Cannot vend thread_legacy_joining, one instance already outstanding.");
                                }
                            }
                        }
                        join_all(futures).map(|_| Ok(()))
                    }
                }
            })
            .await?;

        Ok(())
    }
}

/// Registers a driver instance with the given LoWPAN service factory registrar
/// and returns a future which services factory requests for the driver.
pub async fn register_and_serve_driver_factory<'a, R, D>(
    name: &str,
    registry: R,
    driver: &'a D,
) -> anyhow::Result<()>
where
    R: fidl_fuchsia_factory_lowpan::FactoryRegisterProxyInterface,
    D: Driver + 'a,
{
    use fidl_fuchsia_factory_lowpan::FactoryDriverMarker;
    use fidl_fuchsia_factory_lowpan::FactoryDriverRequest;

    let (client_ep, server_ep) =
        create_endpoints::<FactoryDriverMarker>().context("Failed to create FIDL endpoints")?;

    registry
        .register(name, client_ep)
        .map(|x| match x {
            Ok(Ok(x)) => Ok(x),
            Ok(Err(err)) => Err(format_err!("Service Error: {:?}", err)),
            Err(err) => Err(err.into()),
        })
        .await
        .context("Failed to register LoWPAN factory driver")?;

    server_ep
        .into_stream()?
        .try_for_each_concurrent(MAX_CONCURRENT, |cmd| async {
            match cmd {
                FactoryDriverRequest::GetFactoryDevice { device_factory, .. } => {
                    if let Some(stream) = device_factory.into_stream().ok() {
                        let _ = driver.serve_to(stream).await;
                    }
                }
            }
            Ok(())
        })
        .await?;

    fx_log_info!("LoWPAN FactoryDriver {:?} Stopped.", name);

    Ok(())
}
