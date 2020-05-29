// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of fuchsia.router.config

use {
    fidl_fuchsia_router_config::{RouterAdminRequestStream, RouterStateRequestStream},
    fuchsia_component::server::{ServiceFs, ServiceObjLocal},
    futures::future::Either,
    futures::stream::{self, Stream},
};

pub(super) enum IncomingFidlRequestStream {
    RouterAdmin(RouterAdminRequestStream),
    RouterState(RouterStateRequestStream),
}

/// Returns a `Stream` of [`IncomingFidlRequestStream`]s.
pub(super) fn new_stream<'a>(
    fs: &'a mut ServiceFs<ServiceObjLocal<'static, IncomingFidlRequestStream>>,
) -> Result<
    Either<
        impl Stream<Item = IncomingFidlRequestStream>,
        impl 'a + Stream<Item = IncomingFidlRequestStream>,
    >,
    anyhow::Error,
> {
    // NetworkManager's FIDL is currently disabled. We may eventually
    // remove the support for it from the codebase entirely, but for the
    // time being, we would still like to exercise the functionality
    // during testing to avoid bitrot.
    if !cfg!(test) {
        warn!("not serving FIDL server in non-test builds, the FIDL stream will never be ready");
        // We return a stream that is always pending so that the caller may assume that
        // the FIDL request stream is not expected to end (which is the case when for
        // non-test builds).
        return Ok(Either::Left(stream::pending()));
    }

    fs.dir("svc")
        .add_fidl_service(IncomingFidlRequestStream::RouterAdmin)
        .add_fidl_service(IncomingFidlRequestStream::RouterState);

    Ok(Either::Right(fs.take_and_serve_directory_handle()?))
}
