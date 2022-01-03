// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::{ClientEnd, Proxy, ServerEnd},
    fidl::AsHandleRef,
    fidl_fuchsia_fuzzer as fuzz,
    fuchsia_async::TimeoutExt,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    fuchsia_zircon as zx,
    futures::{channel::oneshot, lock::Mutex},
    std::collections::HashMap,
    std::sync::Arc,
};

struct Registration {
    sender: Option<oneshot::Sender<ClientEnd<fuzz::ControllerProviderMarker>>>,
    receiver: Option<oneshot::Receiver<ClientEnd<fuzz::ControllerProviderMarker>>>,
}

pub struct Registry {
    registration: Mutex<Registration>,
    providers: Mutex<HashMap<String, Arc<fuzz::ControllerProviderProxy>>>,
}

impl Registry {
    pub fn new() -> Self {
        Self {
            registration: Mutex::new(Registration { sender: None, receiver: None }),
            providers: Mutex::new(HashMap::new()),
        }
    }

    pub async fn register(
        &self,
        provider: ClientEnd<fuzz::ControllerProviderMarker>,
    ) -> Result<(), zx::Status> {
        let sender = self.enqueue().await?;
        match sender.send(provider) {
            Ok(_) => Ok(()),
            Err(_) => Err(zx::Status::CANCELED),
        }
    }

    async fn enqueue(
        &self,
    ) -> Result<oneshot::Sender<ClientEnd<fuzz::ControllerProviderMarker>>, zx::Status> {
        let mut registration = self.registration.lock().await;
        if registration.receiver.is_some() {
            return Err(zx::Status::SHOULD_WAIT);
        }
        let (sender, receiver) = match registration.sender.take() {
            Some(sender) => (sender, None),
            None => {
                let (sender, receiver) =
                    oneshot::channel::<ClientEnd<fuzz::ControllerProviderMarker>>();
                (sender, Some(receiver))
            }
        };
        *registration = Registration { sender: None, receiver: receiver };
        Ok(sender)
    }

    pub async fn connect(
        &self,
        fuzzer_url: &str,
        controller: ServerEnd<fuzz::ControllerMarker>,
        timeout: i64,
    ) -> i32 {
        match self.get_provider(fuzzer_url, timeout).await {
            Ok(provider) => match provider.connect(controller).await {
                Ok(_) => zx::Status::OK,
                Err(e) => {
                    fx_log_err!("ControllerProvider.Connect failed: {}", e);
                    zx::Status::INTERNAL
                }
            },
            Err(status) => status,
        }
        .into_raw()
    }

    async fn get_provider(
        &self,
        fuzzer_url: &str,
        timeout: i64,
    ) -> Result<Arc<fuzz::ControllerProviderProxy>, zx::Status> {
        if let Some(provider) = self.lookup(fuzzer_url).await {
            match provider
                .as_channel()
                .wait_handle(zx::Signals::CHANNEL_PEER_CLOSED, zx::Time::INFINITE_PAST)
            {
                Ok(_) | Err(zx::Status::BAD_HANDLE) | Err(zx::Status::CANCELED) => {
                    // Provider exists but is disconnected; fall-through and get a new one.
                }
                Err(zx::Status::TIMED_OUT) => {
                    return Ok(provider);
                }
                Err(e) => {
                    fx_log_err!("unexpected error when checking if channel closed: {}", e);
                    return Err(e);
                }
            };
        }
        let deadline = zx::Time::after(zx::Duration::from_nanos(timeout));
        let receiver = self.dequeue().await?;
        let result = receiver.on_timeout(deadline, || Err(oneshot::Canceled)).await;
        let client_end = result.map_err(|_| zx::Status::TIMED_OUT)?;
        self.insert(fuzzer_url, client_end).await
    }

    async fn lookup(&self, fuzzer_url: &str) -> Option<Arc<fuzz::ControllerProviderProxy>> {
        let providers = self.providers.lock().await;
        match providers.get(fuzzer_url) {
            Some(provider) => Some(provider.clone()),
            None => None,
        }
    }

    async fn dequeue(
        &self,
    ) -> Result<oneshot::Receiver<ClientEnd<fuzz::ControllerProviderMarker>>, zx::Status> {
        let mut registration = self.registration.lock().await;
        if registration.sender.is_some() {
            return Err(zx::Status::SHOULD_WAIT);
        }
        let (sender, receiver) = match registration.receiver.take() {
            Some(receiver) => (None, receiver),
            None => {
                let (sender, receiver) =
                    oneshot::channel::<ClientEnd<fuzz::ControllerProviderMarker>>();
                (Some(sender), receiver)
            }
        };
        *registration = Registration { sender: sender, receiver: None };
        Ok(receiver)
    }

    async fn insert(
        &self,
        fuzzer_url: &str,
        client_end: ClientEnd<fuzz::ControllerProviderMarker>,
    ) -> Result<Arc<fuzz::ControllerProviderProxy>, zx::Status> {
        let provider = client_end.into_proxy().map_err(|_| zx::Status::INTERNAL)?;
        let provider = Arc::new(provider);
        let mut providers = self.providers.lock().await;
        providers.insert(fuzzer_url.to_string(), provider.clone());
        Ok(provider)
    }

    pub async fn disconnect(&self, fuzzer_url: &str) -> i32 {
        let mut providers = self.providers.lock().await;
        let result = match providers.remove(fuzzer_url) {
            Some(provider) => match provider.stop() {
                Err(fidl::Error::ClientChannelClosed { .. }) => zx::Status::OK,
                Err(e) => {
                    fx_log_warn!("failed to stop {}: {:?}", fuzzer_url, e);
                    zx::Status::INTERNAL
                }
                Ok(_) => zx::Status::OK,
            },
            None => zx::Status::NOT_FOUND,
        };
        result.into_raw()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::registry::Registry,
        fidl::endpoints::{create_endpoints, create_request_stream},
        fidl::AsHandleRef,
        fidl_fuchsia_fuzzer as fuzz, fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::TryStreamExt,
    };

    // Test fixtures and helpers.

    static URL: &str = "fuchsia-pkg://fuchsia.com/fuzz_manager_tests#meta/registry.cm";

    struct ControllerForTest {
        client_end: Option<ClientEnd<fuzz::ControllerMarker>>,
        server_end: Option<ServerEnd<fuzz::ControllerMarker>>,
    }

    impl ControllerForTest {
        fn new() -> Self {
            let (client_end, server_end) = create_endpoints::<fuzz::ControllerMarker>()
                .expect("failed to create Controller endpoints");
            Self { client_end: Some(client_end), server_end: Some(server_end) }
        }

        fn server_end(&mut self) -> ServerEnd<fuzz::ControllerMarker> {
            self.server_end.take().unwrap()
        }

        fn is_connected(&self) -> bool {
            let result = match self.client_end.as_ref() {
                Some(client_end) => {
                    let channel = client_end.channel();
                    channel.wait_handle(zx::Signals::CHANNEL_PEER_CLOSED, zx::Time::INFINITE_PAST)
                }
                None => Ok(zx::Signals::NONE),
            };
            match result {
                Err(zx::Status::TIMED_OUT) => true,
                _ => false,
            }
        }

        fn close(&mut self) {
            self.client_end.take();
        }
    }

    struct ControllerProviderForTest {
        task: Option<fasync::Task<()>>,
        client_end: Option<ClientEnd<fuzz::ControllerProviderMarker>>,
    }

    impl ControllerProviderForTest {
        fn new() -> Self {
            let (client_end, stream) = create_request_stream::<fuzz::ControllerProviderMarker>()
                .expect("failed to create ControllerProvider request stream");
            let task = fasync::Task::spawn(async move {
                let connected = Arc::new(Mutex::new(None));
                stream
                    .try_for_each(|request| async {
                        let connected_for_request = connected.clone();
                        match request {
                            fuzz::ControllerProviderRequest::Connect { controller, responder } => {
                                {
                                    let mut connected = connected_for_request.lock().await;
                                    *connected = Some(controller);
                                }
                                responder.send()?;
                            }
                            fuzz::ControllerProviderRequest::Stop { .. } => {
                                {
                                    let mut connected = connected_for_request.lock().await;
                                    connected.take();
                                }
                                return Ok(());
                            }
                        };
                        Ok(())
                    })
                    .await
                    .expect("provider error")
            });
            Self { task: Some(task), client_end: Some(client_end) }
        }

        fn client_end(&mut self) -> ClientEnd<fuzz::ControllerProviderMarker> {
            self.client_end.take().unwrap()
        }

        async fn stop(&mut self) -> anyhow::Result<()> {
            self.client_end.take();
            if let Some(task) = self.task.take() {
                task.await;
            }
            Ok(())
        }
    }

    async fn connect(
        registry: &Registry,
        controller: ServerEnd<fuzz::ControllerMarker>,
        timeout_ms: i64,
    ) -> Result<(), zx::Status> {
        let timeout = zx::Duration::from_millis(timeout_ms).into_nanos();
        zx::Status::ok(registry.connect(&URL, controller, timeout).await)
    }

    async fn register_after_delay(
        registry: &Registry,
        provider: ClientEnd<fuzz::ControllerProviderMarker>,
        delay_ms: i64,
    ) -> Result<(), zx::Status> {
        fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(delay_ms))).await;
        registry.register(provider).await
    }

    async fn connect_after_delay(
        registry: &Registry,
        controller: ServerEnd<fuzz::ControllerMarker>,
        timeout_ms: i64,
        delay_ms: i64,
    ) -> Result<(), zx::Status> {
        fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(delay_ms))).await;
        connect(registry, controller, timeout_ms).await
    }

    // Unit tests.

    #[fasync::run_singlethreaded(test)]
    async fn test_register_before_connect() -> anyhow::Result<()> {
        let registry = Registry::new();
        let mut controller = ControllerForTest::new();
        let mut provider = ControllerProviderForTest::new();
        registry.register(provider.client_end()).await?;
        let result = connect(&registry, controller.server_end(), 100).await;
        assert!(result.is_ok());
        assert!(controller.is_connected());
        zx::Status::ok(registry.disconnect(&URL).await)?;
        provider.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_connect_before_register() -> anyhow::Result<()> {
        let registry = Registry::new();
        let mut controller = ControllerForTest::new();
        let mut provider = ControllerProviderForTest::new();
        let result = futures::join!(
            connect(&registry, controller.server_end(), 200),
            register_after_delay(&registry, provider.client_end(), 100),
        );
        assert!(result.0.is_ok());
        assert!(result.1.is_ok());
        assert!(controller.is_connected());
        zx::Status::ok(registry.disconnect(&URL).await)?;
        provider.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_connect_without_register() -> anyhow::Result<()> {
        let registry = Registry::new();
        let mut controller = ControllerForTest::new();
        let mut provider = ControllerProviderForTest::new();
        let result = connect(&registry, controller.server_end(), 100).await;
        assert_eq!(result, Err(zx::Status::TIMED_OUT));
        assert!(!controller.is_connected());
        provider.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_registers() -> anyhow::Result<()> {
        let registry = Registry::new();
        let mut controller = ControllerForTest::new();
        let mut provider1 = ControllerProviderForTest::new();
        let mut provider2 = ControllerProviderForTest::new();
        let result = futures::join!(
            register_after_delay(&registry, provider1.client_end(), 100),
            register_after_delay(&registry, provider2.client_end(), 200),
            connect_after_delay(&registry, controller.server_end(), 500, 300),
        );
        assert!(result.0.is_ok());
        assert_eq!(result.1.err(), Some(zx::Status::SHOULD_WAIT));
        assert!(result.2.is_ok());
        assert!(controller.is_connected());
        zx::Status::ok(registry.disconnect(&URL).await)?;
        provider1.stop().await?;
        provider2.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_connects() -> anyhow::Result<()> {
        let registry = Registry::new();
        let mut controller1 = ControllerForTest::new();
        let mut controller2 = ControllerForTest::new();
        let mut provider = ControllerProviderForTest::new();
        let result = futures::join!(
            connect_after_delay(&registry, controller1.server_end(), 500, 100),
            connect_after_delay(&registry, controller2.server_end(), 500, 200),
            register_after_delay(&registry, provider.client_end(), 300),
        );
        assert!(result.0.is_ok());
        assert_eq!(result.1.err(), Some(zx::Status::SHOULD_WAIT));
        assert!(result.2.is_ok());
        assert!(controller1.is_connected());
        assert!(!controller2.is_connected());
        zx::Status::ok(registry.disconnect(&URL).await)?;
        provider.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_disconnect_without_connect() -> anyhow::Result<()> {
        let registry = Registry::new();
        let result = registry.disconnect(&URL).await;
        assert_eq!(result, zx::Status::NOT_FOUND.into_raw());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_disconnects() -> anyhow::Result<()> {
        let registry = Registry::new();
        let mut controller = ControllerForTest::new();
        let mut provider = ControllerProviderForTest::new();

        registry.register(provider.client_end()).await?;
        connect(&registry, controller.server_end(), 100).await?;
        let result = registry.disconnect(&URL).await;
        assert_eq!(result, zx::Status::OK.into_raw());

        let result = registry.disconnect(&URL).await;
        assert_eq!(result, zx::Status::NOT_FOUND.into_raw());
        provider.stop().await
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_disconnect_closed() -> anyhow::Result<()> {
        let registry = Registry::new();
        let mut controller = ControllerForTest::new();
        let mut provider = ControllerProviderForTest::new();

        registry.register(provider.client_end()).await?;
        connect(&registry, controller.server_end(), 100).await?;
        controller.close();
        assert!(!controller.is_connected());
        zx::Status::ok(registry.disconnect(&URL).await)?;
        provider.stop().await
    }
}
