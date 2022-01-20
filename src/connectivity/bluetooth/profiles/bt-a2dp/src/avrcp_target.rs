// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_bluetooth_component::{LifecycleMarker, LifecycleProxy, LifecycleState},
    fidl_fuchsia_sys::{LauncherMarker, LauncherProxy},
    fuchsia_component::{client, fuchsia_single_component_package_url},
    futures::{future::BoxFuture, FutureExt},
    tracing::info,
};

/// The v1 component URL for the AVRCP-Target component.
const AVRCP_TARGET_URL: &str = fuchsia_single_component_package_url!("bt-avrcp-target");

/// An interface for managing a component that provides the `Lifecycle` protocol.
pub trait LifecycleProxyConnector {
    /// The URL of the component providing the `Lifecycle` protocol.
    const PROTOCOL_PROVIDING_URL: &'static str;

    /// Returns true if the protocol `S` exists in the environment, false if not, or an Error
    /// if checking could not be completed.
    fn check_protocol<S: DiscoverableProtocolMarker>(
        &self,
    ) -> BoxFuture<'static, Result<bool, Error>>;

    /// Return a LauncherProxy that can be used to start up components. An Error will be
    /// returned if the protocol is unavailable.
    /// By default, the environment's LauncherProxy is returned.
    fn launcher(&self) -> Result<LauncherProxy, Error> {
        client::launcher()
    }

    /// Return the Lifecycle protocol that can be used to read component status. An Error will
    /// be returned if the protocol is unavailable.
    /// By default, the environment's LifecycleProxy is returned.
    fn lifecycle(&self) -> Result<LifecycleProxy, Error> {
        client::connect_to_protocol::<LifecycleMarker>()
    }
}

/// The controller for managing the starting of the AVRCP Target component in both
/// CFv1 and CFv2 environments.
pub struct AvrcpTarget;

impl LifecycleProxyConnector for AvrcpTarget {
    const PROTOCOL_PROVIDING_URL: &'static str = AVRCP_TARGET_URL;

    fn check_protocol<S: DiscoverableProtocolMarker>(
        &self,
    ) -> BoxFuture<'static, Result<bool, Error>> {
        async {
            let svc_dir = client::new_protocol_connector::<S>()?;
            svc_dir.exists().await
        }
        .boxed()
    }
}

/// Waits for the LifecycleState to be `Ready` or returns Error if using the
/// `proxy` fails.
async fn lifecycle_wait_ready(proxy: LifecycleProxy) -> Result<(), Error> {
    loop {
        match proxy.get_state().await? {
            LifecycleState::Initializing => continue,
            LifecycleState::Ready => break,
        }
    }
    Ok(())
}

/// Attempts to connect to the `Lifecycle` protocol. Returns the `LifecycleProxy`
/// and an optional App representing the backing component on success, or an Error
/// if the protocol could not be resolved.
async fn get_proxy(
    connector: impl LifecycleProxyConnector,
) -> Result<(LifecycleProxy, Option<client::App>), Error> {
    // First we attempt to retrieve the Lifecycle protocol from A2DP's environment.
    // Typically, in a CFv2 environment, this protocol will exist in the environment.
    if connector.check_protocol::<LifecycleMarker>().await? {
        info!("Found the `Lifecycle` protocol in the environment");
        let lifecycle = connector.lifecycle()?;
        return Ok((lifecycle, None));
    }

    // Otherwise, fallback to trying to get it via the Launcher protocol.
    // This will typically succeed in a CFv1 environment.
    if connector.check_protocol::<LauncherMarker>().await? {
        info!("Found the `Launcher` protocol in the environment");
        let launcher = connector.launcher()?;
        let child = client::launch(
            &launcher,
            <AvrcpTarget as LifecycleProxyConnector>::PROTOCOL_PROVIDING_URL.to_string(),
            None,
        )?;
        let lifecycle = child
            .connect_to_protocol::<LifecycleMarker>()
            .context("failed to connect to child component's Lifecycle protocol")?;
        return Ok((lifecycle, Some(child)));
    }

    // Otherwise, we're unable to resolve the `Lifecycle` protocol.
    Err(format_err!("Couldn't get the Lifecycle protocol"))
}

/// Attempt to start the AVRCP-Target component used to relay media updates to the peer.
/// If the child component was manually launched, returns OK with the component `App`. Dropping the
/// `App` will terminate the child component.
/// If the child component was successfully started (but not manually launched), returns Ok(None).
/// Otherwise, returns Error if any protocols were unavailable or if component starting failed.
pub async fn start_avrcp_target() -> Result<Option<client::App>, Error> {
    let connector = AvrcpTarget;
    let (lifecycle, child) = get_proxy(connector).await?;
    lifecycle_wait_ready(lifecycle).await?;
    Ok(child)
}

#[cfg(test)]
mod tests {
    use super::*;

    use {
        fidl_fuchsia_bluetooth_component::LifecycleRequest,
        fidl_fuchsia_sys::LauncherRequest,
        fuchsia_async as fasync,
        futures::{task::Poll, StreamExt, TryStreamExt},
    };

    /// Mock implementation of a controller that provides the `Lifecycle` protocol.
    /// Provides hooks for toggling the Launcher / Lifecycle protocols for testing purposes.
    struct MockComponentClient {
        supported_services: Vec<String>,
        launcher: Option<LauncherProxy>,
        lifecycle: Option<LifecycleProxy>,
    }

    impl MockComponentClient {
        fn new(launcher: Option<LauncherProxy>, lifecycle: Option<LifecycleProxy>) -> Self {
            let mut supported_services = vec![];
            if launcher.is_some() {
                supported_services.push(LauncherMarker::PROTOCOL_NAME.to_string());
            }
            if lifecycle.is_some() {
                supported_services.push(LifecycleMarker::PROTOCOL_NAME.to_string());
            }
            Self { supported_services, launcher, lifecycle }
        }
    }

    impl LifecycleProxyConnector for MockComponentClient {
        /// The URL of the mock component providing the protocol is irrelevant since our tests
        /// manually implement the `Launcher` protocol.
        const PROTOCOL_PROVIDING_URL: &'static str = "foobar";

        fn check_protocol<S: DiscoverableProtocolMarker>(
            &self,
        ) -> BoxFuture<'static, Result<bool, Error>> {
            let contains = self.supported_services.contains(&S::PROTOCOL_NAME.to_string());
            async move { Ok(contains) }.boxed()
        }

        fn launcher(&self) -> Result<LauncherProxy, Error> {
            self.launcher.clone().ok_or(format_err!("Launcher protocol not available"))
        }

        fn lifecycle(&self) -> Result<LifecycleProxy, Error> {
            self.lifecycle.clone().ok_or(format_err!("Lifecycle protocol not available"))
        }
    }

    #[fuchsia::test]
    async fn avrcp_target_no_protocols_returns_error() {
        // Simulate error by providing neither protocol. Attempting to launch should return error.
        let mock = MockComponentClient::new(None, None);
        let res = get_proxy(mock).await;
        assert!(res.is_err());
    }

    #[fuchsia::test]
    async fn avrcp_target_in_v2_returns_ok() {
        let (c, _s) = fidl::endpoints::create_proxy_and_stream::<LifecycleMarker>().unwrap();

        // Simulate v2 environment by not providing the Launcher protocol.
        let mock = MockComponentClient::new(None, Some(c));

        // A request to launch AVRCP-TG in the "v2 scenario" should work, there is no returned
        // `App` because this is v2.
        let (_proxy, child) = get_proxy(mock).await.expect("launching should work");
        assert!(child.is_none());
    }

    #[fuchsia::test]
    async fn avrcp_target_in_v1_returns_ok() {
        let (c, mut s) = fidl::endpoints::create_proxy_and_stream::<LauncherMarker>().unwrap();

        // Simulate v1 environment by not providing the Lifecycle protocol.
        let mock = MockComponentClient::new(Some(c), None);

        // Handle the request to launch the component using the `Launcher` protocol.
        fasync::Task::local(async move {
            if let Some(req) = s.try_next().await.unwrap() {
                info!("Received launch request: {:?}", req);
                match req {
                    LauncherRequest::CreateComponent { .. } => {}
                }
            }
        })
        .detach();

        // A request to launch AVRCP-TG in the "v1 scenario" should work - the launched App should
        // be returned.
        let (_proxy, child) = get_proxy(mock).await.expect("launching should work");
        assert!(child.is_some());
    }

    #[fuchsia::test]
    async fn avrcp_target_in_v1_error_when_launcher_error() {
        let (c, s) = fidl::endpoints::create_proxy_and_stream::<LauncherMarker>().unwrap();

        // Simulate v1 environment by not providing the Lifecycle protocol.
        let mock = MockComponentClient::new(Some(c), None);

        // Dropping the ServerEnd of the Launcher protocol will result in errors in any client
        // requests.
        drop(s);

        // A request launch when there is a fatal error when accessing the Launcher protocol will
        // return an Error.
        let res = get_proxy(mock).await;
        assert!(res.is_err());
    }

    #[test]
    fn waiting_for_lifecycle_returns_when_ready() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<LifecycleMarker>().unwrap();
        let mut wait_fut = Box::pin(lifecycle_wait_ready(proxy));

        // We expect a request to get the current state of the Lifecycle service.
        // Initially respond with Initializing (e.g the child component is not ready yet.)
        assert!(exec.run_until_stalled(&mut wait_fut).is_pending());
        match exec.run_until_stalled(&mut stream.next()) {
            Poll::Ready(Some(Ok(LifecycleRequest::GetState { responder, .. }))) => {
                responder.send(LifecycleState::Initializing).unwrap();
            }
            x => panic!("Expected GetState request but got: {:?}", x),
        }

        // Should still be waiting. This time, we respond with Ready.
        // Respond with ready - future should resolve.
        assert!(exec.run_until_stalled(&mut wait_fut).is_pending());
        match exec.run_until_stalled(&mut stream.next()) {
            Poll::Ready(Some(Ok(LifecycleRequest::GetState { responder, .. }))) => {
                responder.send(LifecycleState::Ready).unwrap();
            }
            x => panic!("Expected GetState request but got: {:?}", x),
        }
        let result = exec.run_until_stalled(&mut wait_fut);
        assert_matches::assert_matches!(result, Poll::Ready(Ok(_)));
    }

    #[test]
    fn waiting_for_lifecycle_returns_error_when_client_error() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<LifecycleMarker>().unwrap();
        let mut wait_fut = Box::pin(lifecycle_wait_ready(proxy));
        assert!(exec.run_until_stalled(&mut wait_fut).is_pending());

        // Dropping the server end will result in any client requests to resolve to Error.
        drop(stream);

        let result = exec.run_until_stalled(&mut wait_fut);
        assert_matches::assert_matches!(result, Poll::Ready(Err(_)));
    }
}
