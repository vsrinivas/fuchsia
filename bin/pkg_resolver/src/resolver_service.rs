// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::endpoints::{RequestStream, ServerEnd};
use fidl_fuchsia_amber::ControlProxy as AmberProxy;
use fidl_fuchsia_io::{self, DirectoryMarker, DirectoryProxy};
use fidl_fuchsia_pkg::{PackageResolverRequest, PackageResolverRequestStream, UpdatePolicy};
use fuchsia_async as fasync;
use fuchsia_pkg_uri::PackageUri;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn};
use fuchsia_zircon::{Channel, MessageBuf, Signals, Status};
use futures::prelude::*;

pub async fn run_resolver_service(
    amber: AmberProxy, pkgfs: DirectoryProxy, chan: fasync::Channel,
) -> Result<(), Error> {
    let mut stream = PackageResolverRequestStream::from_channel(chan);

    while let Some(event) = await!(stream.try_next())? {
        let PackageResolverRequest::Resolve {
            package_uri,
            selectors,
            update_policy,
            dir,
            responder,
        } = event;

        let status = await!(resolve(
            &amber,
            &pkgfs,
            package_uri,
            selectors,
            update_policy,
            dir
        ));
        responder.send(Status::from(status).into_raw())?;
    }

    Ok(())
}

/// Resolve the package.
///
/// FIXME: at the moment, we are proxying to Amber to resolve a package name and variant to a
/// merkleroot. Because of this, we cant' implement the update policy, so we just ignore it.
async fn resolve<'a>(
    amber: &'a AmberProxy, pkgfs: &'a DirectoryProxy, pkg_uri: String, selectors: Vec<String>,
    _update_policy: UpdatePolicy, dir_request: ServerEnd<DirectoryMarker>,
) -> Result<(), Status> {
    fx_log_info!("resolving {} with the selectors {:?}", pkg_uri, selectors);

    let uri = PackageUri::parse(&pkg_uri).map_err(|err| {
        fx_log_err!("failed to parse package uri: {}", err);
        Err(Status::INVALID_ARGS)
    })?;

    // FIXME: at the moment only the fuchsia.com host is supported.
    if uri.host() != "fuchsia.com" {
        fx_log_warn!("package uri's host is currently unsupported: {}", uri);
    }

    // FIXME: need to implement selectors.
    if !selectors.is_empty() {
        fx_log_warn!("resolve does not support selectors yet");
    }

    // While the fuchsia-pkg:// spec doesn't require a package name, we do.
    let name = uri.name().ok_or_else(|| {
        fx_log_err!("package uri is missing a package name: {}", uri);
        Err(Status::INVALID_ARGS)
    })?;

    // Ask amber to cache the package.
    let chan =
        await!(amber.get_update_complete(&name, uri.variant(), uri.hash())).map_err(|err| {
            fx_log_err!("error communicating with amber: {:?}", err);
            Status::INTERNAL
        })?;

    let merkle = await!(wait_for_update_to_complete(chan)).map_err(|err| {
        fx_log_err!("error when waiting for amber to complete: {:?}", err);
        Status::INTERNAL
    })?;

    fx_log_info!("success: {} has a merkle of {}", name, merkle);

    // FIXME: this is a bit of a hack but there isn't a formal way to convert a Directory request
    // into a Node request.
    let node_request = ServerEnd::new(dir_request.into_channel());

    let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY;
    pkgfs.open(flags, 0, &merkle, node_request).map_err(|err| {
        fx_log_err!("error opening {}: {:?}", merkle, err);
        Status::INTERNAL
    })?;

    Ok(())
}

async fn wait_for_update_to_complete(chan: Channel) -> Result<String, Status> {
    let mut buf = MessageBuf::new();

    let sigs = await!(fasync::OnSignals::new(
        &chan,
        Signals::CHANNEL_PEER_CLOSED | Signals::CHANNEL_READABLE
    ))?;

    if sigs.contains(Signals::CHANNEL_READABLE) {
        chan.read(&mut buf)?;
        let buf = buf.split().0;

        if sigs.contains(Signals::USER_0) {
            let msg = String::from_utf8_lossy(&buf);
            fx_log_err!("error installing package: {}", msg);
            return Err(Status::INTERNAL);
        }

        let merkle = match String::from_utf8(buf) {
            Ok(merkle) => merkle,
            Err(err) => {
                let merkle = String::from_utf8_lossy(err.as_bytes());
                fx_log_err!("{:?} is not a valid merkleroot: {:?}", merkle, err);

                return Err(Status::INTERNAL);
            }
        };

        Ok(merkle)
    } else {
        fx_log_err!("response channel closed unexpectedly");
        Err(Status::INTERNAL)
    }
}
