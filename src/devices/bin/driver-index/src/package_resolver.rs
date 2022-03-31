// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context;
use futures::TryStreamExt;
use {
    fidl_fuchsia_io as fio, fidl_fuchsia_pkg::PackageResolverRequestStream,
    fuchsia_url::pkg_url::PkgUrl, futures::StreamExt,
};
pub async fn serve(stream: PackageResolverRequestStream) -> anyhow::Result<()> {
    stream
        .map(|result| result.context("failed request"))
        .try_for_each(|request| async {
            match request {
                fidl_fuchsia_pkg::PackageResolverRequest::Resolve {
                    package_url,
                    dir,
                    responder,
                } => {
                    let package_url = PkgUrl::parse(&package_url)?;
                    let root_url = package_url.root_url();
                    let package_name = io_util::canonicalize_path(root_url.path());
                    let flags = fio::OPEN_RIGHT_READABLE | fio::OPEN_FLAG_DIRECTORY;
                    io_util::node::connect_in_namespace(
                        &format!("/pkgfs/packages/{}/0", package_name),
                        flags,
                        dir.into_channel(),
                    )?;
                    responder.send(&mut Ok(())).context("error sending response")?;
                }
                fidl_fuchsia_pkg::PackageResolverRequest::GetHash { package_url: _, responder } => {
                    responder
                        .send(&mut Err(fuchsia_zircon::sys::ZX_ERR_UNAVAILABLE))
                        .context("error sending response")?;
                }
            }
            Ok(())
        })
        .await?;
    Ok(())
}
