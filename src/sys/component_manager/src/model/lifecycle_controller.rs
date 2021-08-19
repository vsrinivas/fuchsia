// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        component::{BindReason, ComponentInstance},
        error::ModelError,
        model::Model,
    },
    ::routing::error::ComponentInstanceError,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
    futures::prelude::*,
    log::*,
    moniker::{
        AbsoluteMoniker, AbsoluteMonikerBase, MonikerError, RelativeMoniker, RelativeMonikerBase,
    },
    std::{
        convert::TryFrom,
        sync::{Arc, Weak},
    },
};

#[derive(Clone)]
pub struct LifecycleController {
    model: Weak<Model>,
    prefix: AbsoluteMoniker,
}

#[derive(Debug)]
enum LifecycleOperation {
    Bind,
    Resolve,
    Stop,
}

impl LifecycleController {
    pub fn new(model: Weak<Model>, prefix: AbsoluteMoniker) -> Self {
        Self { model, prefix }
    }

    async fn perform_operation(
        &self,
        operation: LifecycleOperation,
        moniker: String,
        recursive_stop: bool,
    ) -> Result<(), fcomponent::Error> {
        let relative_moniker =
            RelativeMoniker::try_from(moniker.as_str()).map_err(|e: MonikerError| {
                debug!("lifecycle controller received invalid component moniker: {}", e);
                fcomponent::Error::InvalidArguments
            })?;
        if !relative_moniker.up_path().is_empty() {
            debug!(
                "lifecycle controller received moniker that attempted to reach outside its scope"
            );
            return Err(fcomponent::Error::InvalidArguments);
        }
        let abs_moniker = AbsoluteMoniker::from_relative(&self.prefix, &relative_moniker).map_err(
            |e: MonikerError| {
                debug!("lifecycle controller received invalid component moniker: {}", e);
                fcomponent::Error::InvalidArguments
            },
        )?;
        let model = self.model.upgrade().ok_or(fcomponent::Error::Internal)?;

        let component = model.look_up(&abs_moniker).await.map_err(|e| match e {
            e @ ModelError::ResolverError { .. } | e @ ModelError::ComponentInstanceError {
                err: ComponentInstanceError::ResolveFailed { .. }
            } => {
                debug!(
                    "lifecycle controller failed to resolve component instance {}: {:?}",
                    abs_moniker,
                    e
                );
                fcomponent::Error::InstanceCannotResolve
            }
            e @ ModelError::ComponentInstanceError {
                err: ComponentInstanceError::InstanceNotFound { .. },
            } => {
                debug!(
                    "lifecycle controller was asked to perform an operation on a component instance that doesn't exist {}: {:?}",
                    abs_moniker,
                    e,
                );
                fcomponent::Error::InstanceNotFound
            }
            e => {
                error!(
                    "unexpected error encountered by lifecycle controller while looking up component {}: {:?}",
                    abs_moniker,
                    e,
                );
                fcomponent::Error::Internal
            }
        })?;
        match operation {
            LifecycleOperation::Resolve => Ok(()),
            LifecycleOperation::Bind => {
                let _: Arc<ComponentInstance> =
                    component.bind(&BindReason::Debug).await.map_err(|e: ModelError| {
                        debug!(
                            "lifecycle controller failed to bind to component instance {}: {:?}",
                            abs_moniker, e
                        );
                        fcomponent::Error::InstanceCannotStart
                    })?;
                Ok(())
            }
            LifecycleOperation::Stop => {
                component.stop_instance(false, recursive_stop).await.map_err(|e: ModelError| {
                    debug!(
                        "lifecycle controller failed to stop component instance {} (recursive_stop={}): {:?}",
                        abs_moniker, recursive_stop, e
                    );
                    fcomponent::Error::Internal
                })
            }
        }
    }

    pub async fn serve(&self, mut stream: fsys::LifecycleControllerRequestStream) {
        while let Ok(Some(operation)) = stream.try_next().await {
            match operation {
                fsys::LifecycleControllerRequest::Resolve { moniker, responder } => {
                    let mut res =
                        self.perform_operation(LifecycleOperation::Resolve, moniker, false).await;
                    let _ = responder.send(&mut res);
                }
                fsys::LifecycleControllerRequest::Bind { moniker, responder } => {
                    let mut res =
                        self.perform_operation(LifecycleOperation::Bind, moniker, false).await;
                    let _ = responder.send(&mut res);
                }
                fsys::LifecycleControllerRequest::Stop { moniker, responder, is_recursive } => {
                    let mut res = self
                        .perform_operation(LifecycleOperation::Stop, moniker, is_recursive)
                        .await;
                    let _ = responder.send(&mut res);
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::testing::test_helpers::{TestEnvironmentBuilder, TestModelResult},
        cm_rust_testing::ComponentDeclBuilder,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
        std::sync::Arc,
    };

    #[fuchsia::test]
    async fn lifecycle_controller_test() {
        let components = vec![
            (
                "root",
                ComponentDeclBuilder::new()
                    .add_child(cm_rust::ChildDecl {
                        name: "a".to_string(),
                        url: "test:///a".to_string(),
                        startup: fsys::StartupMode::Eager,
                        environment: None,
                        on_terminate: None,
                    })
                    .add_child(cm_rust::ChildDecl {
                        name: "cant-resolve".to_string(),
                        url: "cant-resolve://cant-resolve".to_string(),
                        startup: fsys::StartupMode::Eager,
                        environment: None,
                        on_terminate: None,
                    })
                    .build(),
            ),
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_child(cm_rust::ChildDecl {
                        name: "b".to_string(),
                        url: "test:///b".to_string(),
                        startup: fsys::StartupMode::Eager,
                        environment: None,
                        on_terminate: None,
                    })
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().build()),
        ];

        let TestModelResult { model, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        let lifecycle_controller = LifecycleController::new(Arc::downgrade(&model), vec![].into());

        let (lifecycle_proxy, lifecycle_request_stream) =
            create_proxy_and_stream::<fsys::LifecycleControllerMarker>().unwrap();

        // async move {} is used here because we want this to own the lifecycle_controller
        let _lifecycle_server_task = fasync::Task::local(async move {
            lifecycle_controller.serve(lifecycle_request_stream).await
        });

        assert_eq!(lifecycle_proxy.resolve(".").await.unwrap(), Ok(()));

        assert_eq!(lifecycle_proxy.resolve("./a:0").await.unwrap(), Ok(()));

        assert_eq!(
            lifecycle_proxy.resolve(".\\scope-escape-attempt:0").await.unwrap(),
            Err(fcomponent::Error::InvalidArguments)
        );

        assert_eq!(
            lifecycle_proxy.resolve("./doesnt-exist:0").await.unwrap(),
            Err(fcomponent::Error::InstanceNotFound)
        );

        assert_eq!(
            lifecycle_proxy.resolve("./cant-resolve:0").await.unwrap(),
            Err(fcomponent::Error::InstanceCannotResolve)
        );
    }
}
