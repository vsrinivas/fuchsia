// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Result},
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_fuzzer as fuzz, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::channel::oneshot,
    futures::{pin_mut, select, FutureExt, StreamExt},
    std::cell::RefCell,
    std::collections::HashMap,
    std::fmt::Display,
    std::rc::Rc,
    tracing::{error, warn},
    url::Url,
};

enum ProviderStatus {
    Stopped,
    Launching(oneshot::Sender<()>),
    Running(fuzz::ControllerProviderProxy),
    Connecting,
    Interrupted,
}

pub struct FuzzRegistry {
    providers: Rc<RefCell<HashMap<Url, ProviderStatus>>>,
}

impl FuzzRegistry {
    pub fn new() -> Self {
        Self { providers: Rc::new(RefCell::new(HashMap::new())) }
    }

    pub async fn serve_registrar(&self, stream: fuzz::RegistrarRequestStream) {
        const MAX_CONCURRENT: usize = 100;
        stream
            .for_each_concurrent(MAX_CONCURRENT, |request| async {
                let result = match request {
                    Ok(fuzz::RegistrarRequest::Register { fuzzer_url, provider, responder }) => {
                        if let Ok(url) = parse_url(fuzzer_url) {
                            self.register(url, provider).expect("failed to register");
                        };
                        responder.send()
                    }
                    Err(e) => Err(e),
                };
                if let Some(e) = result.err() {
                    warn!("failed to serve fuchsia.fuzzer.Registrar request: {:?}", e);
                }
            })
            .await;
    }

    pub async fn serve_registry(&self, stream: fuzz::RegistryRequestStream) {
        const MAX_CONCURRENT: usize = 100;
        stream
            .for_each_concurrent(MAX_CONCURRENT, |request| async {
                let result = match request {
                    Ok(fuzz::RegistryRequest::Connect {
                        fuzzer_url,
                        controller,
                        timeout,
                        responder,
                    }) => {
                        let result = match parse_url(fuzzer_url) {
                            Ok(url) => self.connect(url, controller, timeout).await,
                            Err(e) => Err(e),
                        };
                        responder.send(map_zx_result(result))
                    }
                    Ok(fuzz::RegistryRequest::Disconnect { fuzzer_url, responder }) => {
                        let result = match parse_url(fuzzer_url) {
                            Ok(url) => self.disconnect(url).await,
                            Err(e) => Err(e),
                        };
                        responder.send(map_zx_result(result))
                    }
                    Err(e) => Err(e),
                };
                if let Some(e) = result.err() {
                    warn!("failed to serve fuchsia.fuzzer.Registry request: {:?}", e);
                }
            })
            .await;
    }

    // Receives a provider client from a newly started fuzzer.
    fn register(
        &self,
        url: Url,
        provider: ClientEnd<fuzz::ControllerProviderMarker>,
    ) -> Result<()> {
        let mut providers = self.providers.borrow_mut();
        let entry = providers.entry(url).or_insert(ProviderStatus::Stopped);
        match *entry {
            ProviderStatus::Interrupted => {
                // Consider the following sequence of FIDL requests:
                //   connect, disconnect, register, connect
                // The "register after disconnect" is ambiguous: is it a late register associated
                // with the first, interrupted connect or an early register that arrived before the
                // second, valid connect? Without a way to differentiate, the only safe approach is
                // to always drop the register in this case, and let the second `connect` time out.
                // In practice, this is not expected to occur often.
                warn!("Dropping potentially stale registration from previous disconnection.");
                warn!("The current connection attempt may time out and need to be retried.");
                *entry = ProviderStatus::Stopped;
            }
            _ => {
                // For anything else, replace it with the running provider.
                let provider = provider.into_proxy().context("failed to create proxy")?;
                *entry = ProviderStatus::Running(provider);
            }
        };
        Ok(())
    }

    // Attempt to find a provider with the given |url| to use to connect the given |controller|. If
    // the registry doesn't currently have such a provider, it will wait up to |timeout| nanoseconds
    // for a starting fuzzer to register such a provider via `fuchsia.fuzzer.Registrar/Register`.
    async fn connect(
        &self,
        url: Url,
        controller: ServerEnd<fuzz::ControllerMarker>,
        timeout: i64,
    ) -> Result<(), zx::Status> {
        // Try to extract the provider from the map within the timeout.
        let provider_fut = self.get_provider(&url).fuse();
        let timer_fut = fasync::Timer::new(zx::Duration::from_nanos(timeout)).fuse();
        pin_mut!(provider_fut, timer_fut);
        let provider = select! {
            result = provider_fut => result,
            _ = timer_fut => Err(zx::Status::TIMED_OUT),
        }?;
        // Connect. Make sure no mutable RefCells are held across the await.
        let result = provider.connect(controller).await;
        let mut providers = self.providers.borrow_mut();
        match (result, providers.remove(&url)) {
            (Ok(_), Some(ProviderStatus::Connecting)) => {
                // Put the provider back in the map.
                providers.insert(url.clone(), ProviderStatus::Running(provider));
                Ok(())
            }
            (Ok(_), _) => {
                // Only reachable via a concurrent call to `disconnect`.
                Err(zx::Status::CANCELED)
            }
            (Err(e), _) => {
                error!("fuchsia.fuzzer.ControllerProvider:Connect failed: {:?}", e);
                let _ = stop_provider(&url, provider);
                Err(zx::Status::INTERNAL)
            }
        }
    }

    // Promises to return the |ControllerProvider| for the given |url|, possibly by waiting
    // for it to finish starting and to `register` itself.
    async fn get_provider(&self, url: &Url) -> Result<fuzz::ControllerProviderProxy, zx::Status> {
        let receiver = {
            let mut providers = self.providers.borrow_mut();
            let entry = providers.entry(url.clone()).or_insert(ProviderStatus::Stopped);
            match entry {
                ProviderStatus::Stopped | ProviderStatus::Interrupted => {
                    // The provider hasn't registered yet.
                    let (sender, receiver) = oneshot::channel::<()>();
                    *entry = ProviderStatus::Launching(sender);
                    Some(receiver)
                }
                ProviderStatus::Launching(_) | ProviderStatus::Connecting => {
                    // Another call to `connect` is in progress.
                    return Err(zx::Status::SHOULD_WAIT);
                }
                ProviderStatus::Running(_) => None,
            }
        };
        // Wait for the fuzzer to register the provider and drop the channel.
        if let Some(receiver) = receiver {
            let _ = receiver.await;
        }
        let mut providers = self.providers.borrow_mut();
        match providers.insert(url.clone(), ProviderStatus::Connecting) {
            Some(ProviderStatus::Running(provider)) => Ok(provider),
            Some(ProviderStatus::Interrupted) => {
                // Only reachable via a concurrent call to `disconnect`.
                providers.insert(url.clone(), ProviderStatus::Interrupted);
                Err(zx::Status::CANCELED)
            }
            None => {
                // Only reachable via a concurrent call to `disconnect`.
                Err(zx::Status::CANCELED)
            }
            _ => unreachable!(),
        }
    }

    // Removes the provider from the registry.
    pub async fn disconnect(&self, url: Url) -> Result<(), zx::Status> {
        let mut providers = self.providers.borrow_mut();
        match providers.remove(&url) {
            Some(ProviderStatus::Launching(_)) => {
                // There may be an outstanding `register` request that needs to be dropped. See
                // also the note in `register`. The re-insertion is expensive, but this case is
                // uncommon.
                providers.insert(url, ProviderStatus::Interrupted);
                Ok(())
            }
            Some(ProviderStatus::Running(provider)) => stop_provider(&url, provider),
            Some(_) => Ok(()),
            None => Err(zx::Status::NOT_FOUND),
        }
    }
}

fn parse_url<S: AsRef<str> + Display>(url: S) -> Result<Url, zx::Status> {
    Url::parse(url.as_ref()).map_err(|e| {
        warn!("failed to parse {}: {:?}", url, e);
        zx::Status::INVALID_ARGS
    })
}

fn stop_provider(url: &Url, provider: fuzz::ControllerProviderProxy) -> Result<(), zx::Status> {
    match provider.stop() {
        Err(fidl::Error::ClientChannelClosed { .. }) => Ok(()),
        Err(e) => {
            warn!("failed to stop {}: {:?}", url, e);
            Err(zx::Status::INTERNAL)
        }
        Ok(_) => Ok(()),
    }
}

fn map_zx_result(result: Result<(), zx::Status>) -> i32 {
    match result {
        Ok(()) => zx::Status::OK.into_raw(),
        Err(e) => e.into_raw(),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy, create_proxy_and_stream, create_request_stream},
        // fidl::endpoints::{create_endpoints, , create_proxy, create_proxy_and_stream},
        // fidl::AsHandleRef,
        fidl_fuchsia_fuzzer as fuzz,
        fuchsia_async as fasync,
        fuchsia_zircon as zx,
        futures::{channel::oneshot, join, Future, TryStreamExt},
    };

    // Test fixtures and helpers.

    static FOO_URL: &str = "fuchsia-pkg://fuchsia.com/fuzz-manager-unittests#meta/foo.cm";
    static BAR_URL: &str = "fuchsia-pkg://fuchsia.com/fuzz-manager-unittests#meta/bar.cm";

    // Serve a controller provider for a fake fuzzer.
    async fn serve_controller_provider(stream: fuzz::ControllerProviderRequestStream) {
        let (sender, receiver) = oneshot::channel::<()>();
        let sender_rc = Rc::new(RefCell::new(Some(sender)));
        let receiver_rc = Rc::new(RefCell::new(Some(receiver)));
        const MAX_CONCURRENT: usize = 10;
        stream
            .for_each_concurrent(MAX_CONCURRENT, |request| async {
                match request {
                    Ok(fuzz::ControllerProviderRequest::Connect { controller, responder }) => {
                        responder.send().expect("failed to send response");
                        let receiver = Rc::clone(&receiver_rc);
                        let receiver_fut = || async move {
                            let receiver = receiver.borrow_mut().take().unwrap();
                            let _ = receiver.await;
                        };
                        let receiver_fut = receiver_fut().fuse();
                        let controller = controller.into_stream().expect("failed to create stream");
                        let controller_fut = serve_controller(controller).fuse();
                        pin_mut!(receiver_fut, controller_fut);
                        select! {
                            _ = receiver_fut => {}
                            _ = controller_fut => {}
                        };
                    }
                    Ok(fuzz::ControllerProviderRequest::Stop { .. }) => {
                        let sender = Rc::clone(&sender_rc);
                        let sender = sender.borrow_mut().take();
                        if let Some(sender) = sender {
                            let _ = sender.send(());
                        }
                    }
                    Err(e) => unreachable!("ControllerProvider request error: {:?}", e),
                }
            })
            .await;
    }

    // Serve a controller for a fake fuzzer. Detailed fuzzer behavior isn't needed for testing the
    // registry, so this implements the barest minimum to be able to demonstrate connectivity.
    async fn serve_controller(stream: fuzz::ControllerRequestStream) {
        stream
            .try_for_each(|request| async {
                match request {
                    fuzz::ControllerRequest::GetStatus { responder } => {
                        responder.send(fuzz::Status::EMPTY)
                    }
                    _ => unreachable!("unsupported request"),
                }
            })
            .await
            .expect("Fake controller encountered an unexpected error");
    }

    // Runs a registry and registrar while executing the given |test_fut|. The |test_fut| will be
    // given FIDL proxies for `fuchsia.fuzzer.Registry` and `fuchsia.fuzzer.Registrar`.
    async fn serve_test_fut<F, Fut>(test_fut: F)
    where
        F: FnOnce(fuzz::RegistryProxy, fuzz::RegistrarProxy) -> Fut,
        Fut: Future<Output = ()>,
    {
        let fuzz_registry = FuzzRegistry::new();
        let (registry, stream) = create_proxy_and_stream::<fuzz::RegistryMarker>()
            .expect("failed to create proxy and/or stream");
        let serve_registry_fut = fuzz_registry.serve_registry(stream).fuse();
        let (registrar, stream) = create_proxy_and_stream::<fuzz::RegistrarMarker>()
            .expect("failed to create proxy and/or stream");
        let serve_registrar_fut = fuzz_registry.serve_registrar(stream).fuse();
        let test_fut = test_fut(registry, registrar).fuse();
        pin_mut!(serve_registry_fut, serve_registrar_fut, test_fut);
        // The registry futures run indefinitely. Drop them when |test_fut| completes.
        select! {
            _ = serve_registry_fut => {},
            _ = serve_registrar_fut => {},
            _ = test_fut => {},
        };
    }

    // Converts milliseconds to nanoseconds for use in `fuchsia.fuzzer.Registry/Connect`.
    fn timeout_ms(milliseconds: i64) -> i64 {
        zx::Duration::from_millis(milliseconds).into_nanos()
    }

    // Delays the calling future for the given number of |milliseconds|.
    async fn delay_ms(milliseconds: i64) {
        fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(milliseconds))).await;
    }

    // Unit tests.

    #[fuchsia::test]
    async fn test_register_before_connect() {
        let (controller, server_end) =
            create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
        let (client_end, stream) = create_request_stream::<fuzz::ControllerProviderMarker>()
            .expect("failed to create stream");
        let test_fut = |registry: fuzz::RegistryProxy, registrar: fuzz::RegistrarProxy| async move {
            registrar.register(&FOO_URL, client_end).await.expect("failed to register");
            let result = registry
                .connect(&FOO_URL, server_end, timeout_ms(100))
                .await
                .expect("failed to connect");
            assert_eq!(result, zx::Status::OK.into_raw());
            controller.get_status().await.expect("failed to get status");
        };
        join!(serve_test_fut(test_fut), serve_controller_provider(stream));
    }

    #[fuchsia::test]
    async fn test_connect_before_register() {
        let (controller, server_end) =
            create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
        let (client_end, stream) = create_request_stream::<fuzz::ControllerProviderMarker>()
            .expect("failed to create stream");
        let test_fut = |registry: fuzz::RegistryProxy, registrar: fuzz::RegistrarProxy| async move {
            let connect_fut = registry.connect(&FOO_URL, server_end, timeout_ms(200));
            let register_fut = || async {
                delay_ms(100).await;
                registrar.register(&FOO_URL, client_end).await
            };
            let results = join!(connect_fut, register_fut());
            let result = results.0.expect("failed to connect");
            assert_eq!(result, zx::Status::OK.into_raw());
            results.1.expect("failed to register");
            controller.get_status().await.expect("failed to get status");
        };
        join!(serve_test_fut(test_fut), serve_controller_provider(stream));
    }

    #[fuchsia::test]
    async fn test_connect_without_register() {
        let (controller, server_end) =
            create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
        let (_, stream) = create_request_stream::<fuzz::ControllerProviderMarker>()
            .expect("failed to create stream");
        let test_fut = |registry: fuzz::RegistryProxy, _| async move {
            let result = registry
                .connect(&FOO_URL, server_end, timeout_ms(100))
                .await
                .expect("failed to connect");
            assert_eq!(result, zx::Status::TIMED_OUT.into_raw());
            let result = controller.get_status().await;
            assert!(result.is_err());
        };
        join!(serve_test_fut(test_fut), serve_controller_provider(stream));
    }

    #[fuchsia::test]
    async fn test_multiple_registers() {
        let (controller, server_end) =
            create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
        let (client_end1, _) = create_request_stream::<fuzz::ControllerProviderMarker>()
            .expect("failed to create stream");
        let (client_end2, stream) = create_request_stream::<fuzz::ControllerProviderMarker>()
            .expect("failed to create stream");
        let test_fut = |registry: fuzz::RegistryProxy, registrar: fuzz::RegistrarProxy| async move {
            // The first provider should be replaced by the second.
            registrar.register(&FOO_URL, client_end1).await.expect("failed to register");
            registrar.register(&FOO_URL, client_end2).await.expect("failed to connect");
            let result = registry
                .connect(&FOO_URL, server_end, timeout_ms(100))
                .await
                .expect("failed to connect");
            assert_eq!(result, zx::Status::OK.into_raw());
            controller.get_status().await.expect("failed to get status");
        };
        // Verify the second provider is valid by only serving it, and not the first.
        join!(serve_test_fut(test_fut), serve_controller_provider(stream));
    }

    #[fuchsia::test]
    async fn test_multiple_connects() {
        let (controller, server_end1) =
            create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
        let (_, server_end2) =
            create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
        let (client_end, stream) = create_request_stream::<fuzz::ControllerProviderMarker>()
            .expect("failed to create stream");
        let test_fut = |registry: fuzz::RegistryProxy, registrar: fuzz::RegistrarProxy| async move {
            // The second connection should return an error that the first is in progress.
            let connect1_fut = registry.connect(&FOO_URL, server_end1, timeout_ms(500));
            let connect2_fut = || async {
                delay_ms(100).await;
                registry.connect(&FOO_URL, server_end2, timeout_ms(300)).await
            };
            let register_fut = || async {
                delay_ms(200).await;
                registrar.register(&FOO_URL, client_end).await
            };
            let results = join!(connect1_fut, connect2_fut(), register_fut());
            let result = results.0.expect("failed to connect");
            assert_eq!(result, zx::Status::OK.into_raw());
            let result = results.1.expect("failed to connect");
            assert_eq!(result, zx::Status::SHOULD_WAIT.into_raw());
            results.2.expect("failed to register");
            controller.get_status().await.expect("failed to get status");
        };
        join!(serve_test_fut(test_fut), serve_controller_provider(stream));
    }

    #[fuchsia::test]
    async fn test_concurrent() {
        let (foo_controller, foo_server_end) =
            create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
        let (foo_client_end, foo_stream) =
            create_request_stream::<fuzz::ControllerProviderMarker>()
                .expect("failed to create stream");
        let (bar_controller, bar_server_end) =
            create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
        let (bar_client_end, bar_stream) =
            create_request_stream::<fuzz::ControllerProviderMarker>()
                .expect("failed to create stream");
        let test_fut = |registry: fuzz::RegistryProxy, registrar: fuzz::RegistrarProxy| async move {
            registrar.register(&FOO_URL, foo_client_end).await.expect("failed to register");
            registrar.register(&BAR_URL, bar_client_end).await.expect("failed to register");
            let result = registry
                .connect(&FOO_URL, foo_server_end, timeout_ms(100))
                .await
                .expect("failed to connect");
            assert_eq!(result, zx::Status::OK.into_raw());
            let result = registry
                .connect(&BAR_URL, bar_server_end, timeout_ms(100))
                .await
                .expect("failed to connect");
            assert_eq!(result, zx::Status::OK.into_raw());
            foo_controller.get_status().await.expect("failed to get status");
            bar_controller.get_status().await.expect("failed to get status");
        };
        join!(
            serve_test_fut(test_fut),
            serve_controller_provider(foo_stream),
            serve_controller_provider(bar_stream),
        );
    }

    #[fuchsia::test]
    async fn test_disconnect_without_connect() {
        let test_fut = |registry: fuzz::RegistryProxy, _| async move {
            let result = registry.disconnect(&FOO_URL).await.expect("failed to disconnect");
            assert_eq!(result, zx::Status::NOT_FOUND.into_raw());
        };
        serve_test_fut(test_fut).await;
    }

    #[fuchsia::test]
    async fn test_multiple_disconnects() {
        let (controller, server_end) =
            create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
        let (client_end, stream) = create_request_stream::<fuzz::ControllerProviderMarker>()
            .expect("failed to create stream");
        let test_fut = |registry: fuzz::RegistryProxy, registrar: fuzz::RegistrarProxy| async move {
            registrar.register(&FOO_URL, client_end).await.expect("failed to register");
            let result = registry
                .connect(&FOO_URL, server_end, timeout_ms(100))
                .await
                .expect("failed to connect");
            assert_eq!(result, zx::Status::OK.into_raw());
            controller.get_status().await.expect("failed to get status");
            let result = registry.disconnect(&FOO_URL).await.expect("failed to disconnect");
            assert_eq!(result, zx::Status::OK.into_raw());
            let result = registry.disconnect(&FOO_URL).await.expect("failed to disconnect");
            assert_eq!(result, zx::Status::NOT_FOUND.into_raw());
        };
        join!(serve_test_fut(test_fut), serve_controller_provider(stream));
    }

    #[fuchsia::test]
    async fn test_disconnect_closed() {
        let (controller, server_end) =
            create_proxy::<fuzz::ControllerMarker>().expect("failed to create proxy");
        let (client_end, stream) = create_request_stream::<fuzz::ControllerProviderMarker>()
            .expect("failed to create stream");
        let test_fut = |registry: fuzz::RegistryProxy, registrar: fuzz::RegistrarProxy| async move {
            registrar.register(&FOO_URL, client_end).await.expect("failed to register");
            let result = registry
                .connect(&FOO_URL, server_end, timeout_ms(100))
                .await
                .expect("failed to connect");
            assert_eq!(result, zx::Status::OK.into_raw());
            controller.get_status().await.expect("failed to get status");
            drop(controller);
            let result = registry.disconnect(&FOO_URL).await.expect("failed to disconnect");
            assert_eq!(result, zx::Status::OK.into_raw());
        };
        join!(serve_test_fut(test_fut), serve_controller_provider(stream));
    }
}
