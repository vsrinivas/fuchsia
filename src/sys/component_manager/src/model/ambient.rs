// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::*,
    cm_rust::CapabilityPath,
    failure::{Error, Fail},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::future::FutureObj,
    lazy_static::lazy_static,
    log::*,
    std::{convert::TryInto, sync::Arc},
};

lazy_static! {
    pub static ref AMBIENT_SERVICES: Vec<&'static CapabilityPath> = vec![&*REALM_SERVICE];
    pub static ref REALM_SERVICE: CapabilityPath = "/svc/fuchsia.sys2.Realm".try_into().unwrap();
}

/// Represents component manager's ambient environment, which provides ambient services to
/// components. An ambient service is a service that is offered by component manager itself,
/// which any component may use.
///
/// The following ambient services are currently implemented:
/// - fuchsia.sys2.Realm
pub trait AmbientEnvironment: Send + Sync {
    /// Serve the ambient `fuchsia.sys2.Realm` service for `realm` over `stream`.
    fn serve_realm_service(
        &self,
        realm: Arc<Realm>,
        stream: fsys::RealmRequestStream,
    ) -> FutureObj<Result<(), AmbientError>>;
}

/// Errors produced by `AmbientEnvironment`.
#[derive(Debug, Fail)]
pub enum AmbientError {
    #[fail(display = "ambient service unsupported: {}", path)]
    ServiceUnsupported { path: String },
    #[fail(display = "ambient service `{}` failed: {}", path, err)]
    ServiceError {
        path: String,
        #[fail(cause)]
        err: Error,
    },
}

impl AmbientError {
    pub fn service_unsupported(path: impl Into<String>) -> AmbientError {
        AmbientError::ServiceUnsupported { path: path.into() }
    }
    pub fn service_error(path: impl Into<String>, err: impl Into<Error>) -> AmbientError {
        AmbientError::ServiceError { path: path.into(), err: err.into() }
    }
}

impl AmbientEnvironment {
    /// Serve the ambient service denoted by `path` over `server_chan`.
    pub async fn serve<'a>(
        ambient: Arc<AmbientEnvironment>,
        realm: Arc<Realm>,
        path: &'a CapabilityPath,
        server_chan: zx::Channel,
    ) -> Result<(), AmbientError> {
        if *path == *REALM_SERVICE {
            let stream = ServerEnd::<fsys::RealmMarker>::new(server_chan)
                .into_stream()
                .expect("could not convert channel into stream");
            fasync::spawn(async move {
                if let Err(e) = await!(ambient.serve_realm_service(realm, stream)) {
                    warn!("serve_realm failed: {}", e);
                }
            });
            Ok(())
        } else {
            Err(AmbientError::service_unsupported(path.to_string()))
        }
    }
}
