// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_sys as fsys, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::LocalComponentHandles,
    futures::{lock::Mutex, prelude::*},
    std::collections::HashMap,
    std::sync::Arc,
    tracing::*,
};

/// A function that instantiates a v1 component, given its `LaunchInfo`.
///
/// This should serve the component's outgoing directory at `LaunchInfo.directory_request`.
pub type CreateComponentFn = Box<dyn FnMut(fsys::LaunchInfo) + Sync + Send + 'static>;

/// A local implementation of a component that serves the `fuchsia.sys.Environment` and
/// `fuchsia.sys.Launcher` protocols used to manage v1 components.
///
/// This is a testing fake for the production appmgr component, used in tests to mock
/// v1 components.
pub struct FakeAppmgr {
    mock_components: Mutex<HashMap<String, CreateComponentFn>>,
}

impl FakeAppmgr {
    pub fn new(mock_components: HashMap<String, CreateComponentFn>) -> Arc<Self> {
        Arc::new(FakeAppmgr { mock_components: Mutex::new(mock_components) })
    }

    /// Serves a local child component that exposes the `fuchsia.sys.Environment`
    /// and `fuchsia.sys.Launcher` protocols.
    pub async fn serve_local_child(
        self: Arc<Self>,
        handles: LocalComponentHandles,
    ) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        let self_ = self.clone();
        fs.dir("svc").add_fidl_service(move |stream: fsys::EnvironmentRequestStream| {
            let self_ = self_.clone();
            fasync::Task::local(async move { self_.serve_environment(stream).await }).detach();
        });
        let self_ = self.clone();
        fs.dir("svc").add_fidl_service(move |stream: fsys::LauncherRequestStream| {
            let self_ = self_.clone();
            fasync::Task::local(async move { self_.serve_launcher(stream).await }).detach();
        });
        fs.serve_connection(handles.outgoing_dir)?;
        fs.collect::<()>().await;
        Ok(())
    }

    async fn serve_environment(self: Arc<Self>, mut stream: fsys::EnvironmentRequestStream) {
        match stream.try_next().await.expect("failed to serve Environment") {
            Some(fsys::EnvironmentRequest::CreateNestedEnvironment {
                control_handle: _,
                environment,
                controller,
                ..
            }) => {
                let self_ = self.clone();
                fasync::Task::local(async move {
                    let stream =
                        environment.into_stream().expect("failed to create stream for Environment");
                    self_.serve_environment(stream).await
                })
                .detach();

                let self_ = self.clone();
                fasync::Task::local(async move {
                    let (stream, ctrl) = controller
                        .into_stream_and_control_handle()
                        .expect("failed to create stream for EnvironmentController");
                    ctrl.send_on_created().expect("failed to send OnCreated");
                    self_.serve_environment_controller(stream).await
                })
                .detach();
            }
            Some(fsys::EnvironmentRequest::GetLauncher { launcher, .. }) => {
                let self_ = self.clone();
                fasync::Task::local(async move {
                    let stream =
                        launcher.into_stream().expect("failed to create stream for Launcher");
                    self_.serve_launcher(stream).await
                })
                .detach();
            }
            None => {}
            _ => panic!("unexpected Environment request"),
        }
    }

    async fn serve_environment_controller(
        self: Arc<Self>,
        mut stream: fsys::EnvironmentControllerRequestStream,
    ) {
        match stream.try_next().await.expect("failed to serve EnvironmentController") {
            None => {}
            _ => panic!("unexpected EnvironmentController request"),
        }
    }

    async fn serve_launcher(self: Arc<Self>, mut stream: fsys::LauncherRequestStream) {
        while let Some(fsys::LauncherRequest::CreateComponent {
            launch_info,
            controller,
            control_handle: _,
        }) = stream.try_next().await.expect("failed to serve Launcher")
        {
            info!("Launching mock v1 component: {}", launch_info.url);

            match self.mock_components.lock().await.get_mut(&launch_info.url) {
                Some(create_component_fn) => create_component_fn(launch_info),
                None => panic!("unexpected v1 component launched: {}", launch_info.url),
            }

            let (mut controller_stream, ctrl) = controller
                .unwrap()
                .into_stream_and_control_handle()
                .expect("failed to create stream of ComponentController requests");
            fasync::Task::spawn(async move {
                if let Some(request) = controller_stream.try_next().await.unwrap() {
                    panic!("Unexpected ComponentController request: {:?}", request);
                }
            })
            .detach();

            ctrl.send_on_directory_ready().expect("failed to send OnDirectoryReady");
        }
    }
}
