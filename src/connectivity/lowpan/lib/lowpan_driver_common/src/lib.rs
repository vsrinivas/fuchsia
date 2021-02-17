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
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_lowpan_device::{DriverMarker, DriverRequest, DriverRequestStream};
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

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_endpoints;
    use fidl_fuchsia_lowpan_thread::LegacyJoiningMarker;
    use fuchsia_async as fasync;
    use futures::task::{Context, Poll};

    #[derive(Default, Debug)]
    struct Yield(bool);

    impl Future for Yield {
        type Output = ();
        fn poll(mut self: core::pin::Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<Self::Output> {
            if self.as_ref().0 {
                Poll::Ready(())
            } else {
                self.as_mut().0 = true;
                Poll::Pending
            }
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_legacy_joining_mutual_exclusion() {
        let device = DummyDevice::default();

        let (client_ep, server_ep) =
            create_endpoints::<DriverMarker>().context("Failed to create FIDL endpoints").unwrap();

        let server_future = device.serve_to(server_ep.into_stream().unwrap());

        let client_future = async move {
            let driver_proxy = client_ep.into_proxy().unwrap();

            let (client1_ep, server1_ep) = create_endpoints::<LegacyJoiningMarker>().unwrap();

            driver_proxy
                .get_protocols(fidl_fuchsia_lowpan_device::Protocols {
                    thread_legacy_joining: Some(server1_ep),
                    ..fidl_fuchsia_lowpan_device::Protocols::EMPTY
                })
                .unwrap();

            let client1_proxy = client1_ep.into_proxy().unwrap();

            client1_proxy.make_joinable(0, 0).await.unwrap();

            let (client2_ep, server2_ep) = create_endpoints::<LegacyJoiningMarker>().unwrap();

            driver_proxy
                .get_protocols(fidl_fuchsia_lowpan_device::Protocols {
                    thread_legacy_joining: Some(server2_ep),
                    ..fidl_fuchsia_lowpan_device::Protocols::EMPTY
                })
                .unwrap();

            let client2_proxy = client2_ep.into_proxy().unwrap();

            // This should fail since server1_proxy is outstanding.
            assert!(client2_proxy.make_joinable(0, 0).await.is_err());

            client1_proxy.make_joinable(0, 0).await.unwrap();

            // Drop client1_proxy so that we can make sure we can get another.
            std::mem::drop(client1_proxy);

            // This is needed to give the server future a chance to clean itself up.
            Yield::default().await;
            Yield::default().await;

            let (client3_ep, server3_ep) = create_endpoints::<LegacyJoiningMarker>().unwrap();

            driver_proxy
                .get_protocols(fidl_fuchsia_lowpan_device::Protocols {
                    thread_legacy_joining: Some(server3_ep),
                    ..fidl_fuchsia_lowpan_device::Protocols::EMPTY
                })
                .unwrap();

            let client3_proxy = client3_ep.into_proxy().unwrap();

            // This should work since client1_proxy is gone.
            client3_proxy.make_joinable(0, 0).await.unwrap();
        };

        futures::select! {
            err = server_future.boxed_local().fuse() => panic!("Server task stopped: {:?}", err),
            _ = client_future.boxed().fuse() => (),
        }
    }
}
