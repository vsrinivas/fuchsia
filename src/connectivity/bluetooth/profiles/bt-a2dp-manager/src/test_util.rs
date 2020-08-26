// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    async_utils::event::Event,
    fidl::endpoints::*,
    fidl_fuchsia_bluetooth_component::{LifecycleMarker, LifecycleRequest, LifecycleState},
    fidl_fuchsia_io::{DirectoryMarker, DirectoryRequest},
    fidl_fuchsia_sys::{
        ComponentControllerRequestStream, LaunchInfo, LauncherMarker, LauncherProxy,
    },
    futures::{select, FutureExt, StreamExt},
    std::future::Future,
};

/// Based on the expected_role, assert that exactly the expected components are contained in
/// `child_components`.
///
/// Params:
///     $expected_urls: impl Into<&[&str]>
///     $child_components: impl Into<&[ChildComponent>
#[macro_export]
macro_rules! assert_expected_components {
    ($expected_urls:expr, $child_components:expr) => {
        assert_eq!($child_components.len(), $expected_urls.len());
        for c in $child_components {
            assert!(!c.terminated());
            assert!($expected_urls.contains(&c.url()));
        }
    };
}

/// Represents a `ChildComponent` that is spawned by the mock `LauncherProxy`. It can be used to
/// terminate a component and determine termination status, is identified by the url used to request
/// the component, and contains the logic to execute the component as a Future.
pub struct ChildComponent {
    termination: Event,
    launch_info: LaunchInfo,
    controller: Option<ComponentControllerRequestStream>,
}

impl ChildComponent {
    pub fn new(
        launch_info: LaunchInfo,
        controller: ComponentControllerRequestStream,
    ) -> ChildComponent {
        ChildComponent { termination: Event::new(), launch_info, controller: Some(controller) }
    }

    /// Send signal that child component should be terminated.
    pub fn terminate(&self) {
        self.termination.signal();
    }

    /// Returns a bool representing whether the component has been terminated.
    pub fn terminated(&self) -> bool {
        self.termination.signaled()
    }

    /// Returns the url used to create the component
    pub fn url(&self) -> &str {
        &self.launch_info.url
    }

    /// Handle service directory requests. Implements the Lifecycle protocol and responds with
    /// `LifecycleState::Ready` for all requests.
    async fn lifecycle_always_ready(server: impl Into<ServerEnd<LifecycleMarker>>) {
        let mut stream = server.into().into_stream().unwrap();
        while let Some(request) = stream.next().await {
            if let Ok(LifecycleRequest::GetState { responder }) = request {
                let _ = responder.send(LifecycleState::Ready);
            } else {
                unimplemented!();
            }
        }
    }

    async fn service_directory_handler(svc_dir: impl Into<ServerEnd<DirectoryMarker>>) {
        let mut stream = svc_dir.into().into_stream().unwrap();
        while let Some(request) = stream.next().await {
            match request {
                Ok(DirectoryRequest::Open { object, path, .. })
                    if path == LifecycleMarker::NAME =>
                {
                    fasync::Task::spawn(Self::lifecycle_always_ready(object.into_channel()))
                        .detach();
                }
                _ => unimplemented!("No other request type expected"),
            }
        }
    }

    async fn component_controller_handler(mut stream: ComponentControllerRequestStream) {
        while let Some(request) = stream.next().await {
            match request {
                Ok(fidl_fuchsia_sys::ComponentControllerRequest::Kill { control_handle }) => {
                    let _ = control_handle
                        .send_on_terminated(0, fidl_fuchsia_sys::TerminationReason::Exited);
                }
                _ => unimplemented!("Unhandled control request"),
            }
        }
    }

    /// Returns a `Future` that lives until explicitly terminated via
    /// `ComponentControllerRequest::Kill` or the `ChildComponent::terminate` method.
    /// It supports the `fuchsia.bluetooth.component.Lifecycle` FIDL protocol.
    pub fn execute(&mut self) -> impl Future<Output = ()> + 'static {
        let stream = self.controller.take().expect("cannot execute multiple times");
        let svc_dir = self.launch_info.directory_request.take().unwrap();
        let termination = self.termination.clone();

        async move {
            select! {
                _ = Self::component_controller_handler(stream).fuse() => (),
                _ = Self::service_directory_handler(svc_dir).fuse() => (),
                _ = termination.wait() => (),
            };
            termination.signal();
        }
    }
}

/// A `Launcher` provides a test with a way of accessesing `ChildComponent` objects that have been
/// created by the associated `LauncherProxy`. All
pub struct Launcher {
    launched_components: mpsc::UnboundedReceiver<ChildComponent>,
}

impl Launcher {
    /// Wait for and collect the next `n` `ChildComponents` spawned.
    pub async fn next_n(&mut self, n: usize) -> Vec<ChildComponent> {
        (&mut self.launched_components).take(n).collect().await
    }
}

/// Create a new `Launcher` and `LauncherProxy`. The proxy handles requests by spawning
/// components as new async tasks and passing a `ChildComponent` object to the associated
/// `Launcher`.
pub fn mock_launcher() -> (Launcher, LauncherProxy) {
    let (tx, rx) = mpsc::unbounded();
    let launcher = Launcher { launched_components: rx };

    let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<LauncherMarker>().unwrap();
    fasync::Task::spawn(stream.for_each_concurrent(None, move |request| {
        let mut tx = tx.clone();
        async move {
            let fidl_fuchsia_sys::LauncherRequest::CreateComponent {
                launch_info, controller, ..
            } = request.expect("Error in receiving LauncherRequest");
            let controller_request_stream = controller.unwrap().into_stream().unwrap();
            let mut child = ChildComponent::new(launch_info, controller_request_stream);
            let execute = child.execute();
            let _ = tx.send(child).await;
            execute.await;
        }
    }))
    .detach();
    (launcher, proxy)
}
