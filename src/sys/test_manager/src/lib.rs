// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints::{self, DiscoverableService, ServerEnd},
    fidl_fuchsia_component::Error as ComponentError,
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test as ftest,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest::SuiteMarker,
    ftest_manager::{LaunchError, SuiteControllerRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::client,
    futures::prelude::*,
    thiserror::Error,
    tracing::error,
    uuid::Uuid,
};

/// Error encountered running test manager
#[derive(Debug, Error)]
pub enum TestManagerError {
    #[error("Error sending response: {:?}", _0)]
    Response(fidl::Error),

    #[error("Error serving test manager protocol: {:?}", _0)]
    Stream(fidl::Error),

    #[error("Cannot convert to request stream: {:?}", _0)]
    IntoStream(fidl::Error),
}

/// Start test manager and serve it over `stream`.
pub async fn run_test_manager(
    mut stream: ftest_manager::HarnessRequestStream,
) -> Result<(), TestManagerError> {
    while let Some(event) = stream.try_next().await.map_err(TestManagerError::Stream)? {
        match event {
            ftest_manager::HarnessRequest::LaunchSuite {
                test_url,
                options: _,
                suite,
                controller,
                responder,
            } => {
                let controller = match controller.into_stream() {
                    Err(error) => {
                        error!(%error, component_url = %test_url, "invalid controller channel");
                        responder
                            .send(&mut Err(LaunchError::InvalidArgs))
                            .map_err(TestManagerError::Response)?;
                        // process next request
                        continue;
                    }
                    Ok(c) => c,
                };

                match RunningTest::launch_test(&test_url, suite).await {
                    Ok(test) => {
                        responder.send(&mut Ok(())).map_err(TestManagerError::Response)?;
                        fasync::Task::spawn(async move {
                            test.serve_controller(controller).await.unwrap_or_else(|error| {
                                error!(%error, component_url = %test_url, "serve_controller failed");
                            });
                        })
                        .detach();
                    }
                    Err(e) => {
                        responder.send(&mut Err(e)).map_err(TestManagerError::Response)?;
                    }
                }
            }
        }
    }
    Ok(())
}

struct RunningTest {
    child: Option<fsys::ChildRef>,
}

impl Drop for RunningTest {
    fn drop(&mut self) {
        let child = self.child.take();
        fasync::Task::spawn(async move {
            Self::destroy_test(child)
                .await
                .unwrap_or_else(|error| error!(%error, "cannot destroy test child"));
        })
        .detach();
    }
}

impl RunningTest {
    /// Destroy `test_name` and remove it from realm.
    async fn destroy_test(child_ref: Option<fsys::ChildRef>) -> Result<(), Error> {
        if let Some(mut child_ref) = child_ref {
            let realm = client::connect_to_service::<fsys::RealmMarker>()
                .context("Cannot connect to realm service")?;
            realm
                .destroy_child(&mut child_ref)
                .await
                .context("error calling destroy_child")?
                .map_err(|e| format_err!("destroy_child failed: {:?}", e))?;
        }
        Ok(())
    }

    /// Serves Suite controller and destroys this test afterwards.
    pub async fn serve_controller(
        mut self,
        mut stream: SuiteControllerRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await? {
            match event {
                ftest_manager::SuiteControllerRequest::Kill { .. } => {
                    return Self::destroy_test(self.child.take()).await
                }
            }
        }

        Self::destroy_test(self.child.take()).await
    }

    /// Launch test and return the name of test used to launch it in collection.
    pub async fn launch_test(
        test_url: &String,
        suite_request: ServerEnd<SuiteMarker>,
    ) -> Result<Self, LaunchError> {
        let realm = client::connect_to_service::<fsys::RealmMarker>().map_err(|error| {
            error!(%error, "Cannot connect to realm service");
            LaunchError::InternalError
        })?;

        let name = format!("test-{}", Uuid::new_v4().to_string());
        let mut collection_ref = fsys::CollectionRef { name: "tests".to_string() };
        let child_decl = fsys::ChildDecl {
            name: Some(name.clone()),
            url: Some(test_url.clone()),
            startup: Some(fsys::StartupMode::Lazy),
            environment: None,
            ..fsys::ChildDecl::EMPTY
        };
        realm
            .create_child(&mut collection_ref, child_decl)
            .await
            .map_err(|error| {
                error!(%error, component_url = %test_url, "Failed to call realm to instantiate test component");
                LaunchError::InternalError
            })?
            .map_err(|e| match e {
                ComponentError::InvalidArguments
                | ComponentError::CollectionNotFound
                | ComponentError::InstanceAlreadyExists => {
                    error!(component_url = %test_url, "Failed to instantiate test component because an instance with the same name already exists (this should not happen)");
                    LaunchError::InternalError
                }
                ComponentError::ResourceUnavailable => LaunchError::ResourceUnavailable,
                error => {
                    error!(?error, component_url = %test_url, "Failed to instantiate test component due to an unexpected error");
                    LaunchError::InternalError
                }
            })?;

        let mut child_ref =
            fsys::ChildRef { name: name.clone(), collection: Some("tests".to_string()) };

        let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
        realm
            .bind_child(&mut child_ref, server_end)
            .await
            .map_err(|error| {
                error!(%error, component_url = %test_url, "Failed to call realm to bind to test component");
                LaunchError::InternalError
            })?
            .map_err(|e| match e {
                ComponentError::InvalidArguments
                | ComponentError::InstanceNotFound
                | ComponentError::InstanceCannotStart => {
                    error!(component_url = %test_url, "Test component could not be started");
                    LaunchError::InternalError
                }
                ComponentError::InstanceCannotResolve => LaunchError::InstanceCannotResolve,
                error => {
                    error!(?error, component_url = %test_url, "Test component could not be resolved");
                    LaunchError::InternalError
                }
            })?;

        Self::connect_request_to_protocol_at_dir(&dir, suite_request).map_err(|error| {
            error!(
                %error,
                component_url = %test_url,
                "Failed to connect to `fuchsia.test.Suite` protocol"
            );
            LaunchError::InternalError
        })?;
        Ok(RunningTest {
            child: Some(fsys::ChildRef { name: name, collection: Some("tests".to_string()) }),
        })
    }

    /// Connect to an instance of a FIDL protocol hosted in `directory` to `server_end`.
    fn connect_request_to_protocol_at_dir<S: DiscoverableService>(
        directory: &DirectoryProxy,
        server_end: ServerEnd<S>,
    ) -> Result<(), Error> {
        directory
            .open(
                fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
                fidl_fuchsia_io::MODE_TYPE_SERVICE,
                S::SERVICE_NAME,
                ServerEnd::new(server_end.into_channel()),
            )
            .context("Failed to open protocol in directory")
    }
}
