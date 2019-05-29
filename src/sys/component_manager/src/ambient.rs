// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{AmbientEnvironment, AmbientError, Realm, REALM_SERVICE},
    failure::Error,
    fidl_fuchsia_sys2 as fsys,
    futures::future::FutureObj,
    futures::prelude::*,
    log::*,
    std::sync::Arc,
};

/// Provides the implementation of `AmbientEnvironment` which is used in production.
pub struct RealAmbientEnvironment {}

impl AmbientEnvironment for RealAmbientEnvironment {
    fn serve_realm_service(
        &self,
        realm: Arc<Realm>,
        stream: fsys::RealmRequestStream,
    ) -> FutureObj<'static, Result<(), AmbientError>> {
        FutureObj::new(Box::new(async move {
            await!(Self::do_serve_realm_service(realm, stream))
                .map_err(|e| AmbientError::service_error(REALM_SERVICE.to_string(), e))
        }))
    }
}

impl RealAmbientEnvironment {
    pub fn new() -> Self {
        RealAmbientEnvironment {}
    }

    async fn do_serve_realm_service(
        realm: Arc<Realm>,
        mut stream: fsys::RealmRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = await!(stream.try_next())? {
            match request {
                fsys::RealmRequest::BindChild { responder, .. } => {
                    info!("{} binding to child!", realm.abs_moniker);
                    responder.send(&mut Ok(()))?;
                }
                fsys::RealmRequest::CreateChild { responder, .. } => {
                    info!("{} creating child!", realm.abs_moniker);
                    responder.send(&mut Ok(()))?;
                }
                fsys::RealmRequest::DestroyChild { responder, .. } => {
                    info!("{} destroying child!", realm.abs_moniker);
                    responder.send(&mut Ok(()))?;
                }
                fsys::RealmRequest::ListChildren { responder, .. } => {
                    info!("{} listing children!", realm.abs_moniker);
                    responder.send(&mut Ok(()))?;
                }
            }
        }
        Ok(())
    }
}
