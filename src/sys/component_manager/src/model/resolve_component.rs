// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{error::ModelError, model::Model},
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_sys2 as fsys,
    futures::prelude::*,
    moniker::{AbsoluteMoniker, RelativeMoniker},
    std::{convert::TryFrom, sync::Weak},
};

#[derive(Clone)]
pub struct ResolveComponent {
    model: Weak<Model>,
    prefix: AbsoluteMoniker,
}

impl ResolveComponent {
    pub fn new(model: Weak<Model>, prefix: AbsoluteMoniker) -> Self {
        Self { model, prefix }
    }

    pub async fn serve(&self, mut stream: fsys::ResolveComponentRequestStream) {
        while let Ok(Some(fsys::ResolveComponentRequest::Resolve { moniker, responder })) =
            stream.try_next().await
        {
            let mut res = match (self.model.upgrade(), RelativeMoniker::try_from(moniker.as_str()))
            {
                (Some(model), Ok(moniker)) => {
                    match AbsoluteMoniker::from_relative(&self.prefix, &moniker) {
                        Ok(abs_moniker) => {
                            model.look_up(&abs_moniker).await.map(|_| ()).map_err(|e| match e {
                                ModelError::ResolverError { .. } => {
                                    fcomponent::Error::InstanceCannotResolve
                                }
                                _ => fcomponent::Error::Internal,
                            })
                        }
                        _ => Err(fcomponent::Error::InstanceNotFound),
                    }
                }
                _ => Err(fcomponent::Error::InstanceNotFound),
            };
            let _ = responder.send(&mut res);
        }
    }
}
