// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{component::BindReason, error::ModelError, model::Model},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
    futures::prelude::*,
    moniker::{AbsoluteMoniker, RelativeMoniker},
    std::{convert::TryFrom, sync::Weak},
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
        is_recursive: bool,
    ) -> Result<(), fcomponent::Error> {
        println!("Requested lifecycle operation: {:?}, moniker: {}", operation, moniker);
        if let (Some(model), Ok(moniker)) =
            (self.model.upgrade(), RelativeMoniker::try_from(moniker.as_str()))
        {
            if let Ok(abs_moniker) = AbsoluteMoniker::from_relative(&self.prefix, &moniker) {
                match model.look_up(&abs_moniker).await {
                    Ok(component) => match operation {
                        LifecycleOperation::Resolve => {
                            println!("Found component {} and resolving", abs_moniker);
                            Ok(())
                        }
                        LifecycleOperation::Bind => {
                            println!("Found component {} and binding", abs_moniker);
                            component
                                .bind(&BindReason::Debug)
                                .await
                                .map(|_| ())
                                .map_err(|_| fcomponent::Error::Internal)
                        }
                        LifecycleOperation::Stop => {
                            println!("Found component {} and stopping", abs_moniker);
                            component
                                .stop_instance(false, is_recursive)
                                .await
                                .map(|_| ())
                                .map_err(|_| fcomponent::Error::Internal)
                        }
                    },
                    Err(ModelError::ResolverError { .. }) => {
                        Err(fcomponent::Error::InstanceCannotResolve)
                    }
                    Err(_) => Err(fcomponent::Error::Internal),
                }
            } else {
                Err(fcomponent::Error::InstanceNotFound)
            }
        } else {
            Err(fcomponent::Error::InstanceNotFound)
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
